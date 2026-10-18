# Interactive Debugger UI Reference — em836

**em836** provides a 60 FPS hardware-accelerated **Dear ImGui + SDL3** docking interface composed of seven interactive inspection viewports and four configuration modals.

---

## 1. Global Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| **`F5`** | Toggle **Run / Pause** execution (if Halted, automatically resets and restarts from entry point). |
| **`F10`** | **Single Step** one x86 instruction while paused. |
| **`Ctrl+R`** | **Reset CPU & Memory**: zeroes RAM, restores cached binary payload, flushes JIT caches, and resets registers. |
| **`Ctrl+O`** | Open the **Open Program File...** dialog. |
| **`Ctrl+,`** | Open the **Emulator Settings...** dialog. |
| **`Alt+F4`** | Exit **em836**. |

---

## 2. Docking Windows Reference

### 2.1 CPU Registers
Displays a real-time, thread-safe snapshot of the i386 processor state:
- **General-Purpose Registers (32-bit)**: `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, `EDI`.
- **Pointer & Index Registers**: `EIP` (Instruction Pointer), `ESP` (Stack Pointer), `EBP` (Base Pointer).
- **Segment Selectors (16-bit)**: `CS`, `DS`, `ES`, `FS`, `GS`, `SS`.
  > In 32-bit Protected Mode (`UC_MODE_32`), Unicorn maintains flat 4 GB segment descriptors (`D/B=1`). Segment registers are only zeroed when operating in 16-bit Real Mode (`UC_MODE_16`).
- **EFLAGS Bit Badges**: Visual color-coded indicators for `CF` (Carry), `PF` (Parity), `AF` (Auxiliary), `ZF` (Zero), `SF` (Sign), `TF` (Trap), `IF` (Interrupt), `DF` (Direction), and `OF` (Overflow).
- **Instruction Counter**: Total instructions executed since the last reset.

### 2.2 Execution Controls
- **Transport Buttons**: `Run` / `Pause`, `Step` (single instruction), and `Reset`.
- **Clock Speed Throttling**:
  - `Unlimited`: Executes 25,000-instruction quanta per 1 ms yield slice for maximum throughput.
  - `10 Hz`, `100 Hz`, `1 kHz`, `10 kHz`, `100 kHz`, `1 MHz`: Cycle-throttled modes for human-speed visual debugging of loops, register transitions, and drawing routines.
- **CPU Mode Selector**: Switches between `32-bit Protected Mode (i386)` and `16-bit Real Mode (i286/8086)`.
- **VM Status Badge**: High-contrast badge indicating `[ RUNNING ]`, `[ PAUSED ]`, `[ STEPPING ]`, `[ HALTED ]`, or `[ ERROR ]`.

### 2.3 Disassembly (Capstone Engine)
- **Real-Time x86 Disassembly**: Uses Capstone Engine (`CS_ARCH_X86`) to decode instructions from guest RAM into Intel syntax (`mnemonic op_str`) alongside raw hexadecimal opcode bytes.
- **Navigation Toolbar**:
  - `Follow EIP`: Keeps the disassembly viewport tracking `EIP` automatically whenever execution steps outside the visible window.
  - `Goto EIP`: Jumps the disassembly base address directly to current `EIP`.
  - `Origin (0x0)`: Jumps to `0x00000000`.
  - `Address Input`: Hexadecimal address box to inspect instructions at any physical RAM offset.
- **Execution Pointer (`=>`)**: Yellow marker highlighting the instruction at current `EIP`.
- **Interactive Breakpoints (`[BP]`)**: Click any row's checkbox to toggle a hardware-assisted execution breakpoint (`UC_HOOK_CODE`) at that instruction address.

### 2.4 VGA Display (Dual-Mode Canvas)
- Renders the active video mode at 60 FPS while preserving a 4:3 CRT aspect ratio:
  - **Text Mode (`80x25 @ 0xB8000`)**: CP437 8×16 font with 16 foreground and 16 background colors.
  - **Graphics Mode (`640x480 @ 0xA0000`)**: 16-color linear pixel buffer.
- Includes a mode switcher dropdown and a **Clear Screen** button.

### 2.5 Memory Hex Viewer
- **16-Byte Hex & ASCII Table**: Displays physical address, 16 hexadecimal byte columns, and printable ASCII characters.
- **Quick-Jump Buttons**:
  - `Origin 0x0` (`0x00000000`)
  - `EIP` (Current Instruction Pointer)
  - `VGA (0xB8000)` (Text Mode Video RAM)
  - `GFX (0xA0000)` (Graphics Framebuffer RAM)
  - `Stack (ESP)` (Current Stack Top)
- **Page Controls**: `< Prev` and `Next >` buttons step backward/forward through guest RAM.

### 2.6 Console & Output
- **Virtual Terminal Tab**:
  - Displays output from `INT 0x88` (`SYS_PRINT_CHAR`, `SYS_PRINT_STR`, `SYS_WRITE`), legacy interrupts (`INT 0x10`, `INT 0x21`, `INT 0x80`), and I/O ports (`0x03F8`, `0x00E9`).
  - Interactive command & input prompt with up/down command history.
- **AstraC Build Log Tab**:
  - Displays compiler/assembler stdout, stderr, file paths, and exit status from `AstraC.exe`.

### 2.7 Code Editor
- **Language Selector**: Switch between `Assembly (.as)` and `Astra C (.ac)`.
- **`Examples...` Popup**: Automatically locates the repository [`examples/`](../examples/) folder (walking up parent directories automatically when launched from `out/build/x64-debug`) and lists all `.as` and `.ac` sample programs.
- **In-Place File Workflow**:
  - **`Compile & Run`**: Saves edits directly to the active file on disk, invokes [`AstraC.exe`](https://antonako.net/astrac/index.php), invalidates Unicorn's JIT translation cache, loads the new binary, and starts execution.
  - **`Compile Only`**: Compiles and loads the binary into RAM in a paused state at `EIP = 0` for single-stepping.
  - **`Save` / `Reload`**: Saves buffer changes to disk or reloads the file from disk.
  - **Status Indicator**: Displays the active filename (`[graphics_demo.as *]`) with a hover tooltip showing the canonical absolute path.

---

## 3. Modal Dialogs

- **File $\to$ Open Program File... (`Ctrl+O`)**: Enter a path to any `.as`, `.ac`, `.bin`, or `.exe` file to open, compile, and run.
- **File $\to$ Execution Setup / Load Options...**: Customize Load Base Address, Initial `EIP`, and Initial `ESP`.
- **File $\to$ Settings... (`Ctrl+,`)**: Configure custom `AstraC.exe` path, guest physical RAM size (4 MB–64 MB), and default clock frequency (persisted to `config.json`).
- **Help $\to$ About em836**: Version and subsystem overview.