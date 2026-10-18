# Unified System Call ABI (`INT 0x88`) & Interrupt Reference — em836

**em836** provides a deterministic, register-based software interrupt interface on vector **`INT 0x88`** alongside backward-compatible handlers for legacy BIOS (`INT 0x10`), MS-DOS (`INT 0x21`), Linux i386 POSIX (`INT 0x80`), and hardware I/O ports (`0x03F8`, `0x00E9`).

All syscall symbolic constants are defined in [`examples/syscalls.inc`](../examples/syscalls.inc), which can be included directly in both AstraC Assembly (`.as`) and Astra C (`.ac`) programs via `#include "syscalls.inc"` (see the [AstraC Preprocessor Reference](https://antonako.net/astrac/docs/preprocessor.php)).

---

## 1. Calling Convention (`INT 0x88`)

To invoke an **em836** system call:
1. Load the **System Call Identifier (`SYS_*`)** into `EAX`.
2. Load up to 5 arguments into `EBX`, `ECX`, `EDX`, `ESI`, and `EDI`.
3. Execute `INT 0x88`.
4. Read the return status or result from `EAX`.

| Register | Role | Description |
| :--- | :--- | :--- |
| **`EAX`** | **Syscall ID / Return Value** | Input: `SYS_*` constant (`0x01`–`0x44`). Output: Result (`>= 0` on success, `-1` / `0xFFFFFFFF` on error). |
| **`EBX`** | **Argument 1** | Primary parameter (exit code, char, string pointer, file handle, mode, X coordinate). |
| **`ECX`** | **Argument 2** | Secondary parameter (max string length, file flags, buffer pointer, Y coordinate). |
| **`EDX`** | **Argument 3** | Tertiary parameter (byte count, seek origin, width, X1 coordinate, pixel color). |
| **`ESI`** | **Argument 4** | Quaternary parameter (height, Y1 coordinate). |
| **`EDI`** | **Argument 5** | Quinary parameter (line/rectangle palette color index `0`–`15`). |

> **Register Preservation**: All general-purpose registers except `EAX` (which receives the return value) are preserved across `INT 0x88`.

---

## 2. Master Syscall Table (`INT 0x88`)

| Hex ID | Dec ID | Symbolic Name | Category | `EBX` (Arg 1) | `ECX` (Arg 2) | `EDX` (Arg 3) | `ESI` (Arg 4) | `EDI` (Arg 5) | Return (`EAX`) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `0x01` | `1` | `SYS_EXIT` | Process | Exit status code | — | — | — | — | `0` (Halts VM) |
| `0x02` | `2` | `SYS_YIELD` | Process | — | — | — | — | — | `0` |
| `0x03` | `3` | `SYS_GET_TICKS` | Process | — | — | — | — | — | Elapsed ms (`uint32`) |
| `0x10` | `16` | `SYS_PRINT_CHAR` | Console | ASCII char (`BL`) | — | — | — | — | `1` |
| `0x11` | `17` | `SYS_PRINT_STR` | Console | Guest string ptr | Max len (`0`=NUL) | — | — | — | Bytes printed |
| `0x12` | `18` | `SYS_READ_CHAR` | Console | Mode (`0`=poll) | — | — | — | — | Char (`0..255`) or `-1` |
| `0x20` | `32` | `SYS_OPEN` | File I/O | Path string ptr | Flags (`0..2`) | — | — | — | Handle (`>= 3`) or `-1` |
| `0x21` | `33` | `SYS_READ` | File I/O | File handle | Dest buffer ptr | Byte count | — | — | Bytes read or `-1` |
| `0x22` | `34` | `SYS_WRITE` | File I/O | Handle (`1`/`2`/fd) | Src buffer ptr | Byte count | — | — | Bytes written or `-1` |
| `0x23` | `35` | `SYS_CLOSE` | File I/O | File handle | — | — | — | — | `0` or `-1` |
| `0x24` | `36` | `SYS_SEEK` | File I/O | File handle | Signed offset | Origin (`0..2`) | — | — | New file pos or `-1` |
| `0x30` | `48` | `SYS_SBRK` | Memory | Increment bytes | — | — | — | — | Old break ptr or `-1` |
| `0x40` | `64` | `SYS_GFX_SET_MODE` | Graphics | `0`=Text, `1`=Gfx | — | — | — | — | `0` |
| `0x41` | `65` | `SYS_GFX_CLEAR` | Graphics | Color (`0..15`) | — | — | — | — | `0` |
| `0x42` | `66` | `SYS_GFX_PLOT` | Graphics | `X` (`0..639`) | `Y` (`0..479`) | Color (`0..15`) | — | — | `0` |
| `0x43` | `67` | `SYS_GFX_LINE` | Graphics | `X0` | `Y0` | `X1` | `Y1` | Color (`0..15`) | `0` |
| `0x44` | `68` | `SYS_GFX_FILL_RECT` | Graphics | `X` | `Y` | `Width` | `Height` | Color (`0..15`) | `0` |

---

## 3. Detailed Syscall Specifications

### 3.1 Process Control (`0x01` – `0x03`)

#### `SYS_EXIT` (`0x01`)
 Transitions the emulator to `EmulatorStatus::Halted`, stops the execution worker thread, and logs the process exit code to the Virtual Terminal.
- **Inputs**:
  - `EAX = 0x01`
  - `EBX`: Exit status code (e.g., `0` for success).
- **Returns**: `EAX = 0`.
- **Example**:
  ```nasm
  mov eax, 0x01       ; SYS_EXIT
  mov ebx, 0          ; Exit code 0
  int 0x88
  ```

#### `SYS_YIELD` (`0x02`)
Yields the remaining host execution quantum via `std::this_thread::yield()`, allowing cooperative multitasking loops to release CPU time.
- **Inputs**: `EAX = 0x02`
- **Returns**: `EAX = 0`

#### `SYS_GET_TICKS` (`0x03`)
Returns the number of milliseconds elapsed since the emulator initialized SDL3 (`SDL_GetTicks()`). Useful for frame timing, animations, and benchmarking.
- **Inputs**: `EAX = 0x03`
- **Returns**: `EAX = Elapsed milliseconds (uint32_t)`
- **Example Program**: [`examples/process_ticks_demo.as`](../examples/process_ticks_demo.as)

---

### 3.2 Console I/O (`0x10` – `0x12`)

#### `SYS_PRINT_CHAR` (`0x10`)
Appends a single ASCII character to the Virtual Terminal ring buffer.
- **Inputs**:
  - `EAX = 0x10`
  - `EBX`: Character in low byte (`BL = EBX & 0xFF`).
- **Returns**: `EAX = 1`

#### `SYS_PRINT_STR` (`0x11`)
Reads a string from guest physical RAM and writes it to the Virtual Terminal.
- **Inputs**:
  - `EAX = 0x11`
  - `EBX`: Guest physical address of the character buffer.
  - `ECX`: Maximum number of bytes to print (`0` = read until null terminator `'\0'`, up to 64 KB safety limit).
- **Returns**: `EAX = Number of bytes printed`.

#### `SYS_READ_CHAR` (`0x12`)
Dequeues the next available ASCII character from the Virtual Terminal's thread-safe FIFO input queue (populated when the user types text into the Console input bar).
- **Inputs**:
  - `EAX = 0x12`
  - `EBX`: Mode flag (`0` = non-blocking poll).
- **Returns**: `EAX = ASCII character (0..255)` if a keystroke was queued, or `-1` (`0xFFFFFFFF`) if the input queue is empty.
- **Example Program**: [`examples/console_io_demo.as`](../examples/console_io_demo.as)

---

### 3.3 Host File I/O (`0x20` – `0x24`)

All open host file handles are tracked inside `SyscallDispatcher` and automatically closed whenever the VM is reset (`Emulator::Reset()`).

#### `SYS_OPEN` (`0x20`)
Opens a file on the host filesystem.
- **Inputs**:
  - `EAX = 0x20`
  - `EBX`: Guest physical address of null-terminated file path string.
  - `ECX`: Access mode flag:
    - `0` (`O_RDONLY`): Read binary (`"rb"`)
    - `1` (`O_WRONLY`): Write/create/truncate binary (`"wb"`)
    - `2` (`O_APPEND`): Append binary (`"ab"`)
- **Returns**: `EAX = File handle (>= 3)` on success, or `-1` (`0xFFFFFFFF`) on failure.

#### `SYS_READ` (`0x21`)
Reads bytes from an open file handle into guest physical RAM.
- **Inputs**:
  - `EAX = 0x21`
  - `EBX`: Open file handle returned by `SYS_OPEN`.
  - `ECX`: Destination guest physical memory address.
  - `EDX`: Maximum number of bytes to read.
- **Returns**: `EAX = Number of bytes read` (`0` on EOF), or `-1` on invalid handle.

#### `SYS_WRITE` (`0x22`)
Writes bytes from guest physical RAM to an open file handle or standard output/error stream.
- **Inputs**:
  - `EAX = 0x22`
  - `EBX`: File handle (`1` = `FD_STDOUT`, `2` = `FD_STDERR` write directly to Virtual Terminal; `>= 3` writes to host file).
  - `ECX`: Source guest physical memory address.
  - `EDX`: Number of bytes to write.
- **Returns**: `EAX = Number of bytes written`, or `-1` on error.

#### `SYS_CLOSE` (`0x23`)
Flushes and closes an open host file handle.
- **Inputs**:
  - `EAX = 0x23`
  - `EBX`: File handle (`>= 3`).
- **Returns**: `EAX = 0` on success, or `-1` on invalid handle.

#### `SYS_SEEK` (`0x24`)
Repositions the file offset of an open host file handle.
- **Inputs**:
  - `EAX = 0x24`
  - `EBX`: File handle (`>= 3`).
  - `ECX`: Signed byte offset (`long`).
  - `EDX`: Origin constant:
    - `0` (`SEEK_SET`): Beginning of file
    - `1` (`SEEK_CUR`): Current file position
    - `2` (`SEEK_END`): End of file
- **Returns**: `EAX = New file offset from beginning of file`, or `-1` on error.
- **Example Program**: [`examples/file_io_demo.as`](../examples/file_io_demo.as)

---

### 3.4 Dynamic Memory Management (`0x30`)

#### `SYS_SBRK` (`0x30`)
Manages the guest dynamic heap region located between **`0x00200000` (2 MB)** and **`0x00700000` (7 MB)**.
- **Inputs**:
  - `EAX = 0x30`
  - `EBX`: Number of bytes to increment the program break (`0` queries the current program break without modifying it).
- **Returns**: `EAX = Previous program break address` (base address of the newly allocated block), or `-1` (`0xFFFFFFFF`) if the allocation would exceed `0x00700000`.
- **Example Program**: [`examples/heap_sbrk_demo.as`](../examples/heap_sbrk_demo.as)

---

### 3.5 Accelerated Graphics Primitives (`0x40` – `0x44`)

#### `SYS_GFX_SET_MODE` (`0x40`)
Switches the active display mode of the VGA subsystem.
- **Inputs**:
  - `EAX = 0x40`
  - `EBX`:
    - `0` (`VIDEO_MODE_TEXT`): 80×25 16-color Text Mode at `0x000B8000`
    - `1` (`VIDEO_MODE_GFX`): 640×480 16-color Graphics Mode at `0x000A0000`
- **Returns**: `EAX = 0`

#### `SYS_GFX_CLEAR` (`0x41`)
Clears the active video buffer (`0x000A0000` in Graphics Mode or `0x000B8000` in Text Mode).
- **Inputs**:
  - `EAX = 0x41`
  - `EBX`: Palette color index (`0`–`15`).
- **Returns**: `EAX = 0`

#### `SYS_GFX_PLOT` (`0x42`)
Plots a single pixel into the 640×480 graphics framebuffer at `0x000A0000 + (Y * 640) + X`. Out-of-bounds coordinates are safely clipped.
- **Inputs**:
  - `EAX = 0x42`
  - `EBX`: `X` coordinate (`0`–`639`)
  - `ECX`: `Y` coordinate (`0`–`479`)
  - `EDX`: Palette color index (`0`–`15`)
- **Returns**: `EAX = 0`

#### `SYS_GFX_LINE` (`0x43`)
Draws a line between `(X0, Y0)` and `(X1, Y1)` using Bresenham's integer line rasterization algorithm.
- **Inputs**:
  - `EAX = 0x43`
  - `EBX`: `X0` start coordinate
  - `ECX`: `Y0` start coordinate
  - `EDX`: `X1` end coordinate
  - `ESI`: `Y1` end coordinate
  - `EDI`: Palette color index (`0`–`15`)
- **Returns**: `EAX = 0`

#### `SYS_GFX_FILL_RECT` (`0x44`)
Fills a solid axis-aligned rectangle clipped to `640×480`.
- **Inputs**:
  - `EAX = 0x44`
  - `EBX`: Top-left `X` coordinate
  - `ECX`: Top-left `Y` coordinate
  - `EDX`: Rectangle `Width` in pixels
  - `ESI`: Rectangle `Height` in pixels
  - `EDI`: Palette color index (`0`–`15`)
- **Returns**: `EAX = 0`
- **Example Programs**: [`examples/graphics_demo.as`](../examples/graphics_demo.as) and [`examples/astrac_c_demo.ac`](../examples/astrac_c_demo.ac)

---

## 4. Legacy Interrupts & Hardware Port I/O

In addition to `INT 0x88`, **em836** intercepts standard PC/OS interrupts and debug I/O ports so classic assembly examples run unmodified:

### 4.1 BIOS Video Services (`INT 0x10`)
- **`AH = 0x00` (Set Video Mode)**:
  - `AL = 0x03`: Switch to 80×25 16-color Text Mode.
  - `AL = 0x12`: Switch to 640×480 16-color Graphics Mode.
- **`AH = 0x0E` (Teletype Output)**:
  - Writes the ASCII character in `AL` to the Virtual Terminal.

### 4.2 MS-DOS Services (`INT 0x21`)
- **`AH = 0x02` (Character Output)**: Writes the character in `DL` to the Virtual Terminal.
- **`AH = 0x09` (Print String)**: Writes the `'$'`-terminated string starting at guest address `EDX` to the Virtual Terminal.
- **`AH = 0x4C` (Terminate Process)**: Halts VM execution cleanly.

### 4.3 Linux i386 Syscalls (`INT 0x80`)
- **`EAX = 1` (`sys_exit`)**: Halts VM execution with exit status `EBX`.
- **`EAX = 4` (`sys_write`)**: Writes `EDX` bytes from guest buffer `ECX` to file descriptor `EBX` (`1` = stdout, `2` = stderr).

### 4.4 Hardware I/O Ports (`IN` / `OUT`)
- **Port `0x03F8` (COM1 UART Data Register)**:
  - `OUT 0x03F8, AL`: Transmits byte `AL` to the Virtual Terminal.
  - `IN AL, 0x03F8`: Returns `0x00` (ready).
- **Port `0x00E9` (Bochs / QEMU Debug Port)**:
  - `OUT 0x00E9, AL`: Transmits byte `AL` directly to the Virtual Terminal.
- **Example Programs**: [`examples/legacy_interrupts_demo.as`](../examples/legacy_interrupts_demo.as) and [`examples/com1_demo.as`](../examples/com1_demo.as)

---

## 5. Using Syscalls in Astra C (`.ac`)

When writing programs in the [Astra C Language](https://antonako.net/astrac/docs/ac.php), inline `asm { ... }` blocks automatically resolve function parameters and local variables to their `[EBP+offset]` stack locations. You can wrap any `INT 0x88` syscall in a reusable C function:

```c
#include "syscalls.inc"

VOID sys_print_str(PU8 text) {
    asm {
        mov eax, SYS_PRINT_STR
        mov ebx, text
        mov ecx, 0
        int 0x88
    }
    return;
}

VOID sys_gfx_fill_rect(U32 x, U32 y, U32 w, U32 h, U32 color) {
    asm {
        mov eax, SYS_GFX_FILL_RECT
        mov ebx, x
        mov ecx, y
        mov edx, w
        mov esi, h
        mov edi, color
        int 0x88
    }
    return;
}
```

See [`examples/astrac_c_demo.ac`](../examples/astrac_c_demo.ac) for a complete compilable Astra C program.
