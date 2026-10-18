# AstraC Toolchain Integration & Binary Format Specification — em836

**em836** integrates directly with the [**AstraC**](https://antonako.net/astrac/index.php) x86 assembler, disassembler, and C-like compiler toolchain. This allows you to write, edit, assemble/compile, and execute `.as` (Assembly) and `.ac` (Astra C) programs in one click inside the emulator without manual command-line build steps.

---

## 1. Official AstraC Documentation Links

For complete language grammar, instruction sets, preprocessor directives, and binary specifications, refer to the official AstraC documentation at [**https://antonako.net/astrac/index.php**](https://antonako.net/astrac/index.php):

| Topic | Official Documentation URL | Summary |
| :--- | :--- | :--- |
| **AstraC Homepage** | [https://antonako.net/astrac/index.php](https://antonako.net/astrac/index.php) | Toolchain architecture, build pipeline, return codes, and overview |
| **CLI Reference** | [https://antonako.net/astrac/docs/cli.php](https://antonako.net/astrac/docs/cli.php) | Command-line options (`asm`, `comp`, `disasm`, `objdump`, `strdump`, `preproc`, `info`) and flags |
| **AC Language Reference** | [https://antonako.net/astrac/docs/ac.php](https://antonako.net/astrac/docs/ac.php) | Full AC language specification (`U8`/`U16`/`U32`/`F32`/`PU8`, structs, `cdecl` ABI, inline `asm { }`) |
| **Assembler Reference** | [https://antonako.net/astrac/docs/as.php](https://antonako.net/astrac/docs/as.php) | NASM-like Intel syntax, `.code`/`.data`/`.rodata` sections, local labels (`@@`, `@f`, `@b`), `$var` sizes |
| **Disassembler Reference** | [https://antonako.net/astrac/docs/dsm.php](https://antonako.net/astrac/docs/dsm.php) | Linear-sweep 16-bit and 32-bit binary disassembler (`.BIN` $\to$ `.DSM`) |
| **Preprocessor Reference** | [https://antonako.net/astrac/docs/preprocessor.php](https://antonako.net/astrac/docs/preprocessor.php) | `#include`, `#import`, object & function-like `#define`, `#ifdef`/`#if` constant expression evaluation |
| **ACFH Header Spec** | [https://antonako.net/astrac/docs/fileheader.php](https://antonako.net/astrac/docs/fileheader.php) | 108-byte `AC_FILE_HEADER` executable/library container, relocation, export, and import tables |

---

## 2. AstraC Build Pipeline

When **em836** ingests a source file, it invokes `AstraC.exe` according to the file extension:

```text
┌──────────────┐    ┌──────────────┐    ┌───────────────┐
│  Astra C     │───>│  Compiler    │───>│  ASM Source   │
│  (.ac)       │    │  (comp)      │    │  (.as)        │
└──────────────┘    └──────────────┘    └───────────────┘
                                                │
                       ┌────────────────────────┘
                       ▼
                ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
                │ Preprocessor │───>│ Lexer & AST  │───>│ Verify & Opt │
                └──────────────┘    └──────────────┘    └──────────────┘
                                                               │
                       ┌───────────────────────────────────────┘
                       ▼
                ┌──────────────┐    ┌──────────────────────────────────┐
                │   Codegen    │───>│  em836 Binary Loader & Unicorn   │
                │   -> .BIN    │    │  (Flushes TB/TLB & Resets State) │
                └──────────────┘    └──────────────────────────────────┘
```

### Invocation Commands Used by `em836`
- **Assembly (`.as`, `.asm`)**:
  ```powershell
  AstraC.exe asm "<absolute_path_to_file.as>"
  ```
- **Astra C (`.ac`)**:
  ```powershell
  AstraC.exe comp "<absolute_path_to_file.ac>"
  ```

### Canonical Path & Working Directory Resolution
Inside [`AstraCToolchain::CompileFile`](../src/astrac.cpp), **em836** resolves both the `AstraC.exe` binary path and the input source path via `std::filesystem::weakly_canonical(std::filesystem::absolute(...))` and sets the child process working directory to the source file's parent directory. This guarantees that relative `#include "syscalls.inc"` directives inside `examples/` always resolve accurately regardless of where `em836.exe` was launched from.

---

## 3. Toolchain Discovery Hierarchy (`AstraCToolchain::FindAstraC`)

**em836** locates `AstraC.exe` automatically by checking the following paths in priority order:
1. **User-Configured Path**: `astracPath` stored in `config.json` (editable via **File $\to$ Settings...**).
2. **Relative Sibling Build Paths**:
   - `../AstraC/build/Release/AstraC.exe`
   - `../../AstraC/build/Release/AstraC.exe`
   - `../../../AstraC/build/Release/AstraC.exe`
   - `../../../../AstraC/build/Release/AstraC.exe`
3. **Standard Repository Path**: `C:/Users/anton/source/repos/AstraC/build/Release/AstraC.exe`
4. **Local Directory**: `./AstraC.exe` and `build/Release/AstraC.exe`

---

## 4. AstraC Return Codes

When a build runs, stdout/stderr and the process exit code are captured via anonymous Windows pipes and displayed in the **Console & Output $\to$ AstraC Build Log** tab:

| Exit Code | Constant | Meaning |
| :--- | :--- | :--- |
| `0` | `ASTRAC_OK` | Build succeeded |
| `1` | `ASTRAC_ERR_ARGS` | Bad or missing CLI arguments |
| `2` | `ASTRAC_ERR_PREPROCESS` | Preprocessor failure (e.g., missing `#include` file) |
| `3` | `ASTRAC_ERR_LEX` | Lexer tokenization failure |
| `4` | `ASTRAC_ERR_AST` | AST construction / syntax failure |
| `5` | `ASTRAC_ERR_VERIFY` | Semantic or type verification failure |
| `6` | `ASTRAC_ERR_OPTIMIZE` | AST optimization pass failure |
| `7` | `ASTRAC_ERR_CODEGEN` | Machine code generation failure |
| `8` | `ASTRAC_ERR_DISASSEMBLE` | Disassembly failure |
| `9` | `ASTRAC_ERR_COMPILE` | High-level `.ac` compilation failure |

---

## 5. ACFH 108-Byte Executable Header Specification

When AstraC is invoked with the `exe` or `lib` flag, it prepends the 108-byte **ACFH** (`AC_FILE_HEADER`) structure defined at [https://antonako.net/astrac/docs/fileheader.php](https://antonako.net/astrac/docs/fileheader.php):

```cpp
#pragma pack(push, 1)
struct AcFileHeader {
    char magic[4];                  // "ACFH" magic signature
    uint32_t version;               // Format version (1.0 = 0x00010000)
    uint32_t flags;                 // Binary capability flags bitmask
    uint32_t entry_point_offset;    // Binary-relative offset to entry point (_start / _main)
    uint32_t code_offset;           // File offset to .code section
    uint32_t data_offset;           // File offset to .data section
    uint32_t rodata_offset;         // File offset to .rodata section
    uint32_t bss_offset;            // File offset to BSS section
    uint32_t reloc_offset;          // File offset to Relocation Table
    uint32_t func_table_offset;     // File offset to Function Export Table
    uint32_t import_table_offset;   // File offset to Import Table
    uint32_t code_size;             // Size of .code section in bytes
    uint32_t data_size;             // Size of .data section in bytes
    uint32_t rodata_size;           // Size of .rodata section in bytes
    uint32_t bss_size;              // Uninitialized BSS size in bytes (memory-only)
    uint32_t reloc_size;            // Size of Relocation Table in bytes
    uint32_t func_table_size;       // Size of Function Export Table section in bytes
    uint32_t import_table_size;     // Size of Import Table section in bytes
    uint8_t reserved[40];           // Reserved bytes (padded to 108 bytes total)
};
#pragma pack(pop)
```

When [`AstraCToolchain::IngestFile`](../src/astrac.cpp) detects the `"ACFH"` magic bytes:
1. It extracts `code_offset` and `entry_point_offset`.
2. Loads the contiguous binary payload starting at `code_offset` into guest RAM at `0x00000000`.
3. Sets initial `EIP = entry_point_offset`.
4. If the binary does not start with `"ACFH"`, it is treated as a raw flat binary (`.BIN`) and loaded directly at `0x00000000` with `EIP = 0x00000000`.
