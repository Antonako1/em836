# CLI & Ingestion Usage — em836

This guide covers all methods for launching, controlling, compiling, and loading programs into **em836**.

---

## 1. Command-Line Invocation

### Synopsis
```sh
em836 [file_path]
```

### Supported File Types

| File Extension | Pipeline | Description |
|---|---|---|
| `.as`, `.asm` | **AstraC Assembly Source** | Assembled via `AstraC.exe asm <file>`, output `.bin` loaded into memory at origin `0x00000000`, and execution begins immediately. |
| `.ac` | **Astra C Source** | Compiled via `AstraC.exe comp <file>`, output `.bin` loaded, and execution starts immediately. |
| `.bin` | **Flat Raw Binary** | Directly mapped into guest RAM starting at configured origin (`0x00000000` default) and `EIP` initialized. |
| `.exe` | **ACFH Executable** | Parsed according to AstraC's 112-byte `AC_FILE_HEADER`. Entry point and code segments loaded to target offsets automatically. |

### Command-Line Examples

```powershell
# Launch emulator into interactive GUI mode with empty memory:
.\out\build\x64-debug\em836.exe

# Assemble and immediately execute graphics demonstration:
.\out\build\x64-debug\em836.exe examples/graphics_demo.as

# Assemble and run VGA text mode demo:
.\out\build\x64-debug\em836.exe examples/hello.as

# Run a pre-compiled raw binary:
.\out\build\x64-debug\em836.exe kernel.bin
```

---

## 2. Interactive Virtual Terminal CLI

The **Console & Output -> Virtual Terminal** window includes a full emulator command-line interface. Entering commands into the prompt controls the virtual machine in real time:

| Command | Arguments | Description |
|---|---|---|
| `help` | None | Displays full command reference and syntax guide. |
| `run` / `start` | None | Resumes continuous CPU execution. |
| `pause` / `stop`| None | Halts CPU execution. |
| `step` | None | Executes exactly one instruction and pauses. |
| `reset` | None | Resets all CPU registers, peripheral states, and instruction counters. |
| `cls` / `clear` | None | Clears the virtual terminal scrollback buffer. |
| `load <path>` | File Path | Assembles/compiles and loads any supported source or binary file. |
| `speed <hz>` | Clock Speed | Sets clock throttling speed (e.g. `speed 1000` for 1 kHz; `speed 0` for unlimited). |
| `mode <text\|gfx>`| Mode Name | Switches display between `text` (80x25 @ 0xB8000) and `gfx` (640x480 @ 0xA0000). |
| `mem <addr> [len]`| Hex Address | Dumps memory in hex and ASCII at the given physical address. |
| `reg` | None | Prints full CPU register snapshot (`EAX`..`EDI`, `EIP`, `EFLAGS`, flags). |
| `bp <addr>` | Hex Address | Toggles an execution breakpoint at the specified physical address. |

> [!NOTE]
> Any text typed into the prompt that does not match an emulator command is automatically forwarded to the **guest program's input stream**, where it can be consumed via `SYS_READ_CHAR` (`INT 0x88`, `EAX=0x12`).

---

## 3. Drag & Drop Workflow

You can drag and drop any supported file directly from Windows File Explorer into the running `em836` window:
1. When dropping an assembly (`.as`, `.asm`) or Astra C (`.ac`) file:
   - Emulation automatically pauses.
   - The file is assembled/compiled in the background via AstraC.
   - Build messages and compiler diagnostics are recorded in the **AstraC Build Log**.
   - Upon successful build, the emulator resets and executes the resulting `.bin`.
2. When dropping a raw binary (`.bin`) or executable (`.exe`), it is loaded and executed immediately.

---

## 4. In-GUI Embedded Code Editor

The embedded **Code Editor** window allows rapid prototyping without switching to an external text editor:
- **Language Selector**: Choose between `Assembly (.as)` and `Astra C (.ac)`.
- **Compile & Run**: Saves the buffer, compiles via AstraC, and begins execution.
- **Compile Only**: Compiles and displays warnings/errors without modifying the active VM state.
- **Load Sample Code**: Pre-populates the editor with fully working demo templates:
  - *VGA 640x480 Graphics Demo (INT 0x88)*
  - *VGA Text Mode Demo (0xB8000)*
  - *COM1 Serial Port Demo (0x03F8)*
- **Line Numbers Gutter**: Clean monospaced editing with dynamic line number gutters.
