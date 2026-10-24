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

## 5. ACFH 112-Byte Executable & Library Specification

When AstraC is invoked with `type exe` or `type lib`, it prepends the 112-byte **ACFH** (`AC_FILE_HEADER`) structure defined in `AC_FH.h`:

```cpp
#pragma pack(push, 1)
struct AcFileHeader {
    char magic[4];                  // "ACFH" magic signature
    uint32_t version;               // Format version (1.0 = 0x00010000)
    uint32_t flags;                 // Binary capability flags bitmask
    uint32_t entry_point_offset;    // Binary-relative offset to entry point (_start / _main), or 0xFFFFFFFF
    uint32_t code_offset;           // File offset to .code section (typically 112)
    uint32_t data_offset;           // File offset to .data section
    uint32_t rodata_offset;         // File offset to .rodata section
    uint32_t bss_offset;            // File offset to BSS section descriptor
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
    uint8_t reserved[40];           // Reserved bytes (112 bytes total)
};
#pragma pack(pop)
```

### ACFH Flags Bitmask
- `AC_FLAG_EXECUTABLE` (`0x01`): Executable image with entry point (`.exe`).
- `AC_FLAG_DYNAMIC` (`0x02`): Dynamic library module (`.lib`).
- `AC_FLAG_HAS_RELOCS` (`0x04`): Image contains base relocation records.
- `AC_FLAG_HAS_FUNCS` (`0x08`): Image contains exported function symbol table.
- `AC_FLAG_HAS_IMPORTS` (`0x10`): Image contains import references to external `.lib` files.

---

## 6. Dynamic Linking & Library Memory Arena

In **em836**, ACFH dynamic libraries are mapped into a dedicated **Library Memory Arena** above the VGA framebuffers and below the dynamic heap:

- **Main Program Code**: `0x00000000`–`0x0009FFFF`
- **VGA Text & Graphics Buffers**: `0x000A0000`–`0x000EC000`
- **Dynamic Library Arena**: `0x00100000`–`0x001FFFFF` (1 MB allocated page-by-page)
- **Guest Heap (`SYS_SBRK`)**: `0x00200000` upward

### Dynamic Linker Ingestion Pipeline
1. **Executable Loading**: When an `.exe` is loaded, its `.code`, `.data`, and `.rodata` sections are mapped at `0x00000000`.
2. **Automatic Library Resolution**: The emulator inspects the `Import Table` of the executable. For each imported `.lib`, it checks memory first, then searches the executable directory and `examples/`. If a matching source file (`<stem>.ac`) is found on disk without a `.lib`, em836 automatically invokes AstraC with `type lib ot,ft,it` to compile it.
3. **Base Relocation**: When libraries are loaded at `0x00100000+`, the relocation entries in `.code` (disassembled via Capstone) and `.data`/`.rodata` are adjusted by adding the module's runtime base address.
4. **Import Table Patching**: For each `CALL rel32` (`0xE8`) instruction targeting an imported symbol, the relative displacement:
   $$\text{rel32} = \text{targetAddress} - (\text{patchAddress} + 4)$$
   is computed and written directly into guest RAM. Unicorn's translation block caches are subsequently flushed (`uc_ctl_flush_tb`, `uc_ctl_flush_tlb`).
5. **Runaway Protection**: Execution outside the main executable code boundary is permitted inside any loaded library's `.code` range, preventing false runaway halts during inter-module calls.