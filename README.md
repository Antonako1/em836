# em836 — Interactive i386 Emulator & Debugger

**em836** is a high-performance interactive i386 emulator and debugger environment combining:
- **Unicorn Engine**: CPU core emulation supporting 32-bit Protected Mode and 16-bit Real Mode.
- **Capstone Engine**: Real-time x86 instruction disassembly and opcode byte decoding.
- **SDL3**: Hardware-accelerated windowing, input events, and high-DPI GPU texture streaming.
- **Dear ImGui**: Modular debugger interface featuring register inspector, disassembly table, memory hex viewer, virtual terminal, and code editor.
- **Dual-Mode VGA Video**: Standard IBM PC 80×25 text mode (`0x000B8000`) and flat 640×480 16-color linear framebuffer (`0x000A0000`).
- **Unified INT 0x88 Syscall ABI**: Register-based system call interface for process control, terminal I/O, host filesystem access, dynamic memory management (`sbrk`), and accelerated graphics primitives.
- **AstraC Toolchain Integration**: Automated assembly and compilation of Assembly (`.as`, `.asm`) and Astra C (`.ac`) source files into executable binaries.

---

## Features

### 1. Multi-Format Program Ingestion
- **CLI Execution**: `em836.exe examples/graphics_demo.as`
- **Drag & Drop**: Drop any `.as`, `.ac`, `.bin`, or `.exe` onto the application window to compile and execute instantly.
- **In-GUI Code Editor**: Built-in multiline editor with line numbers, demo sample loaders, and one-click "Compile & Run" dispatching to AstraC.
- **ACFH Binary Support**: Parses AstraC 112-byte executable headers, automatically locating code segments and entry points.

### 2. Dual-Mode Video Hardware
- **VGA Text Mode (`0x000B8000`)**: 80×25 character grid with CP437 ASCII glyphs, 16 foreground and 16 background colors rendered via embedded 8×16 IBM PC font.
- **640×480 Graphics Mode (`0x000A0000`)**: 307.2 KB linear 1-byte-per-pixel 16-color framebuffer with software-accelerated drawing primitives (pixel plotting, Bresenham lines, solid rectangle fills).

### 3. Unified Syscall Architecture (`INT 0x88`)
- Register-based ABI (`EAX` = ID, `EBX`..`EDI` = arguments, `EAX` = return value).
- Provides Process services (`SYS_EXIT`, `SYS_YIELD`, `SYS_GET_TICKS`), Console services (`SYS_PRINT_CHAR`, `SYS_PRINT_STR`, `SYS_READ_CHAR`), File I/O (`SYS_OPEN`, `SYS_READ`, `SYS_WRITE`, `SYS_CLOSE`, `SYS_SEEK`), Memory management (`SYS_SBRK`), and Graphics (`SYS_GFX_SET_MODE`, `SYS_GFX_CLEAR`, `SYS_GFX_PLOT`, `SYS_GFX_LINE`, `SYS_GFX_FILL_RECT`).
- Backwards compatible with `INT 0x10` (BIOS video), `INT 0x80` (Linux syscalls), `INT 0x21` (DOS services), and COM1 UART Port `0x03F8`.

### 4. Debugger & Inspection Suite
- Asynchronous execution worker thread with atomic run/pause/step controls keeping the UI responsive at 60 FPS.
- Clock speed throttling: `Unlimited`, `10 Hz`, `100 Hz`, `1 kHz`, `10 kHz`, `100 kHz`, `1 MHz`.
- Real-time Capstone disassembly table with current `EIP` pointer and interactive execution breakpoints.
- CPU register inspector with interactive `EFLAGS` badges and live register editing.
- Scrollable 16-byte memory hex viewer with ASCII dump and quick-jump targets (`0x0`, `EIP`, `VGA`, `GFX`, `ESP`).
- Dual-purpose console: Displays program output and accepts both guest input keystrokes and emulator CLI commands.

---

## Documentation

Comprehensive documentation is available in the [`DOCS/`](DOCS/) directory:

- 📖 [**Memory Map & Hardware Specification**](DOCS/MEMORY_MAP.md) — Physical address spaces, VGA buffer layouts, color palettes, I/O ports, and full INT 0x88 syscall reference.
- 🚀 [**CLI & Ingestion Usage**](DOCS/CLI_USAGE.md) — Command-line flags, virtual terminal commands, drag & drop workflows, and code editor guides.
- ⚙️ [**AstraC Toolchain Integration**](DOCS/ASTRAC_INTEGRATION.md) — Toolchain discovery hierarchy, subprocess pipe redirection, and ACFH executable specifications.
- 🖥️ [**Debugger UI Reference**](DOCS/DEBUGGER_UI.md) — Complete user guide for all 7 docking windows and modal dialogs.
- 🤖 [**AGENTS.md**](AGENTS.md) — Architecture, component boundaries, and developer guidelines.

---

## Building em836

### Prerequisites (Windows)
- Modern C++20 compiler (Visual Studio 2022 MSVC)
- CMake (>= 3.20) and Ninja
- **Unicorn Engine**: `C:/unicorn`
- **SDL3**: `C:/sdl3`
- **Dear ImGui**: `C:/imgui`
- **Capstone Engine**: `C:/capstone`
- **AstraC**: Located at `../AstraC/build/Release/AstraC.exe` or configured in Settings.

### Build Commands (PowerShell)

```powershell
# 1. Initialize Visual Studio x64 environment and configure preset:
cmake --preset x64-debug

# 2. Build target executable:
cmake --build out/build/x64-debug
```

Runtime libraries (`SDL3.dll`, `unicorn.dll`, `capstone.dll`) are automatically copied to `out/build/x64-debug` post-build.

---

## Quick Start

```powershell
# 1. Launch the emulator GUI:
.\out\build\x64-debug\em836.exe

# 2. Or assemble and execute the 640x480 graphics demo immediately:
.\out\build\x64-debug\em836.exe examples/graphics_demo.as

# 3. Or assemble and run the VGA text mode demo:
.\out\build\x64-debug\em836.exe examples/hello.as
```
