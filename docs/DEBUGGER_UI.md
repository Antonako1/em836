# Debugger UI Reference — em836

**em836** features a full Dear ImGui debugger interface providing real-time inspection, hardware controls, memory visualization, interactive disassembly, and code editing.

---

## 1. Window Overview & Layout

```text
+-----------------------------------------------------------------------------------------------------------------+
| File  Emulation  View  Help                                                                                     |
+------------------------------------+------------------------------------+---------------------------------------+
| CPU Registers                      | Execution Controls                 | Code Editor                           |
| EAX: 00000000  EBX: 00000000       | [ Run ] [ Step ] [ Reset ]         | [ Assembly (.as) v ] [Compile & Run]  |
| ECX: 00000000  EDX: 00000000       | Clock Speed: [ Unlimited v ]       | 1 | start:                            |
| ESI: 00000000  EDI: 00000000       | Arch: [ 32-bit Protected Mode v ]  | 2 |     mov eax, 0x40                 |
| ESP: 00800000  EBP: 00800000       | Status: [ RUNNING / PAUSED ]       | 3 |     mov ebx, 1                    |
| EIP: 00000000  EFLAGS: 00000002    | EIP: 0x00000000                    | 4 |     int 0x88                      |
| Flags: [CF][PF][AF][ZF][SF][IF][OF]|                                    |                                       |
+------------------------------------+------------------------------------+---------------------------------------+
| Disassembly (Capstone)                          | VGA Display (Dual Mode: Text 80x25 & GFX 640x480)             |
| [BP] PTR  Address   Bytes        Instruction    | [x] Text Mode (0xB8000)   ( ) Graphics Mode (0xA0000)         |
| [ ]  =>   00000000  B8 40 00 00  mov eax, 0x40  | +-----------------------------------------------------------+ |
| [ ]       00000005  BB 01 00 00  mov ebx, 1     | | VGA 640x480 Output Canvas (4:3 Aspect Ratio)              | |
| [x]       0000000A  CD 88        int 0x88       | +-----------------------------------------------------------+ |
+-------------------------------------------------+---------------------------------------------------------------+
| Memory Hex Viewer                               | Console & Output                                              |
| Jump: [Origin] [EIP] [VGA] [GFX] [Stack] [Addr] | Tab 1: Virtual Terminal  |  Tab 2: AstraC Build Log           |
| 00000000: B8 40 00 00 BB 01 ...  .@....         | > Output stream logs & guest input prompt                     |
+-------------------------------------------------+---------------------------------------------------------------+
```

---

## 2. Window Breakdown

### 1. CPU Registers
- **General Purpose Registers**: Displays `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, `EDI`, `ESP`, and `EBP` in 32-bit hexadecimal. While paused, any register value can be edited directly.
- **Instruction Pointer & Flags**: Displays `EIP` and `EFLAGS`.
- **Interactive EFLAGS Badges**: Color-coded badges for `CF`, `PF`, `AF`, `ZF`, `SF`, `IF`, `DF`, and `OF`. Green denotes `1` (set); dark gray denotes `0` (cleared). Clicking any badge while paused toggles that flag bit in real-time.
- **Segment Registers**: Displays `CS`, `DS`, `ES`, `FS`, `GS`, `SS`.
- **Instruction Counter**: Displays total CPU instructions executed.

### 2. Execution Controls
- **Run / Pause**: Starts continuous asynchronous execution or pauses the VM.
- **Step**: Executes exactly one instruction and pauses.
- **Reset**: Halts execution, resets registers to default values, and clears instruction counters.
- **Clock Speed Throttling Dropdown**:
  - `Unlimited (Max)`: Executes at host CPU speed.
  - `10 Hz`, `100 Hz`, `1 kHz`, `10 kHz`, `100 kHz`, `1 MHz`: Throttles execution rate for human-speed observation of loops, registers, and visual drawing routines.
- **Architecture Selector**: Switches between `32-bit Protected Mode` and `16-bit Real Mode`.
- **Status Indicator**: High-contrast badge showing `[ RUNNING ]`, `[ PAUSED ]`, `[ STEPPING ]`, `[ HALTED ]`, or `[ ERROR ]`.

### 3. Disassembly (Capstone Engine)
- **Live Disassembly**: Decodes x86 instructions surrounding `EIP` in real time using Capstone.
- **`=>` Pointer Marker**: High-contrast yellow arrow highlighting the next instruction about to execute.
- **Interactive Breakpoints (`[BP]`)**: Click any checkbox in the `BP` column to toggle an execution breakpoint at that instruction address. The emulator automatically pauses when a breakpoint is encountered.
- **Opcode Bytes**: Displays instruction opcode and operand bytes in hexadecimal.

### 4. VGA Display (Dual-Mode Canvas)
- **Mode Toggle**: Instantly switch between:
  - `Text Mode (80x25 @ 0xB8000)`: IBM CP437 character grid with 16 IBM PC foreground and background colors rendered via built-in 8×16 font bitmap.
  - `Graphics Mode (640x480 @ 0xA0000)`: Flat 1-byte-per-pixel 16-color linear framebuffer.
- **Clear Screen**: One-click screen wipe.
- **Aspect Ratio Maintenance**: Maintains accurate 4:3 CRT proportion regardless of window resizing.

### 5. Memory Hex Viewer
- **16-Byte Hex Dump**: Scrollable memory view with hexadecimal bytes and printable ASCII sidebar.
- **Quick-Jump Targets**:
  - `Origin 0x0`: Physical address `0x00000000`.
  - `EIP`: Jump to the current instruction pointer.
  - `VGA (0xB8000)`: Jump to text mode memory.
  - `GFX (0xA0000)`: Jump to graphics framebuffer.
  - `Stack (ESP)`: Jump to the active stack top.
- **Address Search**: Hexadecimal input box with Enter key navigation.
- **Page Step**: `< Prev` and `Next >` navigation buttons.

### 6. Console & Output
- **Virtual Terminal Tab**:
  - Displays output from custom syscalls (`SYS_PRINT_CHAR`, `SYS_PRINT_STR`), legacy interrupts (`INT 0x10`, `INT 0x80`, `INT 0x21`), and UART COM1 port writes (`OUT 0x3F8, AL`).
  - Interactive input field: Accepts emulator commands (`help`, `run`, `pause`, `step`, `reset`, `load <file>`, `speed <hz>`, `mode <text|gfx>`, `mem <addr>`, `reg`, `bp <addr>`) or routes keystrokes into the guest application's input queue (`SYS_READ_CHAR`).
  - Auto-scroll and Clear Terminal controls.
- **AstraC Build Log Tab**:
  - Displays stdout and stderr output from AstraC compiler runs, showing exit codes, syntax errors, and symbol tables.

### 7. In-GUI Code Editor
- Multi-line assembly and Astra C editor with line numbers.
- **Compile & Run**: Compiles code buffer directly via AstraC and starts execution in one click.
- **Load Sample Code**: Built-in templates for Graphics Mode, Text Mode, and Serial COM1 demos.

---

## 3. Modal Dialogs

- **File -> Open Program File...**: Load and execute any `.as`, `.ac`, `.bin`, or `.exe` file.
- **File -> Execution Setup / Load Options...**: Configure custom Base Load Address, initial `EIP`, and initial `ESP` before loading.
- **File -> Settings...**: Configure AstraC executable path, total guest RAM size (4 MB to 64 MB), and default clock speeds. Settings persist automatically to `config.json`.
- **Help -> About em836**: Version and component attribution.
