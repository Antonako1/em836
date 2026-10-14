# Memory Map & Hardware Specification — em836

This document defines the physical and virtual memory map, display hardware buffers, I/O ports, peripheral addresses, and interrupt vectors emulated by **em836**.

---

## 1. Overall Address Space (Default 16 MB RAM)

`em836` maps a contiguous 16 MB (`0x01000000` bytes) block of physical address space starting from physical address `0x00000000` up to `0x00FFFFFF`. Sizing is user-configurable from 4 MB up to 64 MB via **File -> Settings...**.

```text
+-----------------------+ 0x01000000 (16 MB Physical RAM Limit)
| Upper Extended Memory |
+-----------------------+ 0x00800000 (8 MB Default Stack Top)
| Stack Region          |  <-- ESP initializes here (grows downward)
+-----------------------+ 0x00700000 (7 MB Max Heap Ceiling)
| Dynamic Heap Region   |  <-- Managed by SYS_SBRK (INT 0x88, EAX=0x30)
+-----------------------+ 0x00200000 (2 MB Initial Heap Break)
| Extended User Code    |
+-----------------------+ 0x00100000 (1 MB High Memory Area)
| Video RAM & ROM Area  |
+-----------------------+ 0x000B8000 - 0x000B8FA0 (4,000 Bytes)
| VGA Text Mode Buffer  |  <-- 80x25 character grid (2 bytes/cell)
+-----------------------+ 0x000A0000 - 0x000EB000 (307,200 Bytes)
| VGA 640x480 GFX Buffer|  <-- Linear 1-byte-per-pixel 16-color framebuffer
+-----------------------+ 0x00010000
| Conventional RAM      |  <-- Configurable payload entry / Protected Mode
+-----------------------+ 0x00007C00
| Real Mode Boot Entry  |  <-- 16-bit bootloader origin
+-----------------------+ 0x00000500
| Free Low RAM          |
+-----------------------+ 0x00000400
| BIOS Data Area (BDA)  |
+-----------------------+ 0x00000000
| IVT / Default Raw Org |  <-- Real Mode IVT & AstraC default raw origin (0x0)
+-----------------------+
```

---

## 2. Memory Regions Breakdown

| Address Range | Size | Region Name | Purpose & Emulation Behavior |
|---|---|---|---|
| `0x00000000 - 0x000003FF` | 1 KB | **Interrupt Vector Table (IVT)** | 256 x 4-byte far pointers (`segment:offset`) for 16-bit Real Mode interrupts. Also serves as the default entry point (`0x0`) for raw flat binaries. |
| `0x00000400 - 0x000004FF` | 256 B | **BIOS Data Area (BDA)** | Standard PC BIOS variables. |
| `0x00007C00 - 0x00007DFF` | 512 B | **Boot Sector (Real Mode)** | Standard entry point when executing 16-bit bootloaders (`.use16` / MBR). |
| `0x00010000 - 0x0009FFFF` | ~576 KB | **Conventional User RAM** | Application code, static data sections, and initial buffers. |
| `0x000A0000 - 0x000EB000` | 307.2 KB | **VGA Graphics Framebuffer** | Flat linear 640×480 16-color graphics buffer (1 byte/pixel, indices 0–15). |
| `0x000B8000 - 0x000B8FA0` | 4,000 B | **VGA Text Mode Buffer** | 80 columns × 25 rows × 2 bytes/cell color character grid. |
| `0x00100000 - 0x001FFFFF` | 1 MB | **High Memory Area (HMA)** | Application code and static variables. |
| `0x00200000 - 0x00700000` | 5 MB | **Dynamic Heap Pool** | Managed dynamically by `SYS_SBRK` (INT 0x88, EAX=0x30). |
| `0x00800000` | — | **Default Stack Top (`ESP`)** | Default initial value of `ESP` in 32-bit Protected Mode. Stack grows downward toward `0x00700000`. |
| `0x00800000 - 0x01000000` | 8 MB | **Upper Extended Memory** | Extended storage space up to total mapped physical RAM. |

---

## 3. Video Subsystem Specifications

`em836` features dual-mode video emulation that can be toggled via the GUI or dynamically selected by guest software via `INT 0x88` (`SYS_GFX_SET_MODE`) or `INT 0x10`.

### A. 640×480 16-Color Graphics Mode (Mode 1)
- **Base Physical Address**: `0x000A0000`
- **Resolution**: 640 columns × 480 rows (307,200 pixels)
- **Memory Layout**: Flat linear array. Exactly 1 byte per pixel:
  $$\text{Address} = 0x000A0000 + (y \times 640) + x$$
- **Color Values**: Bytes contain values `0` through `15`, indexing into the standard IBM PC 16-color palette.
- **Hardware Acceleration Primitives**: Guest programs can plot pixels, draw lines, and fill rectangles directly in RAM or call accelerated syscalls (`SYS_GFX_PLOT`, `SYS_GFX_LINE`, `SYS_GFX_FILL_RECT`).

### B. 80×25 VGA Text Mode (Mode 0)
- **Base Physical Address**: `0x000B8000`
- **Dimensions**: 80 columns × 25 rows = 2,000 character cells
- **Cell Structure (2 Bytes per character cell)**:
  - `Byte 0`: ASCII Character Code (`0x00` - `0xFF`, IBM CP437 encoding).
  - `Byte 1`: Attribute Byte:
    - Bits 7..4: Background Color Index (`0` - `15`).
    - Bits 3..0: Foreground Color Index (`0` - `15`).
- **Rendering**: Scaled to 640×480 with an embedded standard 8×16 IBM PC bitmap font.

### C. IBM PC 16-Color Palette

| Index | Color Name | Hex RGB | Typical Usage |
|---|---|---|---|
| `0` | Black | `#000000` | Default canvas background |
| `1` | Blue | `#0000AA` | Standard blue |
| `2` | Green | `#00AA00` | Standard green |
| `3` | Cyan | `#00AAAA` | Standard cyan |
| `4` | Red | `#AA0000` | Standard red |
| `5` | Magenta | `#AA00AA` | Standard magenta |
| `6` | Brown | `#AA5500` | Brown / dark yellow |
| `7` | Light Gray | `#AAAAAA` | Default console text (`0x07`) |
| `8` | Dark Gray | `#555555` | Dimmed text / shadows |
| `9` | Bright Blue | `#5555FF` | Highlight blue |
| `10` | Bright Green | `#55FF55` | Bright green / success |
| `11` | Bright Cyan | `#55FFFF` | Bright cyan |
| `12` | Bright Red | `#FF5555` | Error red / alert |
| `13` | Bright Magenta | `#FF55FF` | Purple highlight |
| `14` | Yellow | `#FFFF55` | High-contrast yellow |
| `15` | White | `#FFFFFF` | Bright white text |

---

## 4. Custom Unified Syscall ABI (`INT 0x88`)

Software interrupt `INT 0x88` is the primary system call gateway for `em836`.

### Calling Convention:
- **`EAX`**: System Call Identifier (`SYS_*`).
- **`EBX`**: Argument 1.
- **`ECX`**: Argument 2.
- **`EDX`**: Argument 3.
- **`ESI`**: Argument 4.
- **`EDI`**: Argument 5.
- **Return Value**: Returned in `EAX` (non-negative integer on success; negative value on error).

### Syscall Dispatch Table

| Syscall ID (`EAX`) | Symbolic Constant | Arguments | Return (`EAX`) | Description |
|---|---|---|---|---|
| `0x01` | `SYS_EXIT` | `EBX`: Exit Code | `0` | Halts CPU execution and pauses the emulator. |
| `0x02` | `SYS_YIELD` | None | `0` | Yields the remainder of the CPU timeslice on the host. |
| `0x03` | `SYS_GET_TICKS` | None | Ticks (ms) | Returns elapsed host milliseconds since SDL3 initialization. |
| `0x10` | `SYS_PRINT_CHAR` | `EBX`: Character (byte) | `1` | Writes ASCII character to the Virtual Console Terminal. |
| `0x11` | `SYS_PRINT_STR` | `EBX`: String Ptr, `ECX`: Max Len (0=null-term) | Length | Prints null-terminated string to the Virtual Console Terminal. |
| `0x12` | `SYS_READ_CHAR` | `EBX`: 1=block, 0=non-blocking | Char / `-1` | Reads next keystroke from the guest input stream. |
| `0x20` | `SYS_OPEN` | `EBX`: Path Ptr, `ECX`: Flags (0=read, 1=write, 2=append) | Handle / `-1` | Opens a file on the host filesystem. |
| `0x21` | `SYS_READ` | `EBX`: Handle, `ECX`: Buffer Ptr, `EDX`: Count | Bytes read / `-1` | Reads bytes from host file handle into guest RAM. |
| `0x22` | `SYS_WRITE` | `EBX`: Handle, `ECX`: Buffer Ptr, `EDX`: Count | Bytes written / `-1`| Writes bytes from guest RAM to host file handle (1/2 = Console). |
| `0x23` | `SYS_CLOSE` | `EBX`: Handle | `0` / `-1` | Closes open host file handle. |
| `0x24` | `SYS_SEEK` | `EBX`: Handle, `ECX`: Offset, `EDX`: Origin (0=set,1=cur,2=end) | New offset / `-1` | Repositions file handle pointer. |
| `0x30` | `SYS_SBRK` | `EBX`: Increment in bytes | Old break / `-1` | Queries (`EBX=0`) or expands guest dynamic heap allocation. |
| `0x40` | `SYS_GFX_SET_MODE`| `EBX`: Mode (`0`=Text, `1`=Graphics) | `0` | Switches between Text 80x25 and Graphics 640x480. |
| `0x41` | `SYS_GFX_CLEAR` | `EBX`: Color Index (0–15) | `0` | Clears active screen buffer with specified palette color. |
| `0x42` | `SYS_GFX_PLOT` | `EBX`: X, `ECX`: Y, `EDX`: Color (0–15) | `0` | Plots a single pixel at (X, Y) in graphics mode. |
| `0x43` | `SYS_GFX_LINE` | `EBX`: X0, `ECX`: Y0, `EDX`: X1, `ESI`: Y1, `EDI`: Color | `0` | Accelerated Bresenham line drawing primitive. |
| `0x44` | `SYS_GFX_FILL_RECT`| `EBX`: X, `ECX`: Y, `EDX`: W, `ESI`: H, `EDI`: Color | `0` | Accelerated solid rectangle fill primitive. |

---

## 5. Hardware Ports & Legacy Interrupts

`em836` maintains backward compatibility for standard x86 peripherals and legacy software interrupts:

### I/O Ports:
- **`0x03F8` (COM1 UART Serial Data Port)**:
  - `OUT DX, AL`: Appends character directly to the Virtual Console Terminal.
  - `IN AL, DX`: Returns `0x00` (ready status).
- **`0x00E9` (Bochs / QEMU Debug Port)**:
  - `OUT DX, AL`: Direct ASCII output mirrored to the terminal.

### Legacy Software Interrupts:
- **`INT 0x10` (BIOS Video Services)**:
  - `AH = 0x0E`: Teletype character output (`AL` = ASCII char).
  - `AH = 0x00`: Set video mode (`AL = 0x12` for 640x480, `AL = 0x03` for 80x25).
- **`INT 0x80` (Linux POSIX Syscalls)**:
  - `EAX = 1`: `sys_exit(ebx)`.
  - `EAX = 4`: `sys_write(ebx=fd, ecx=buf, edx=len)`.
- **`INT 0x21` (DOS Services)**:
  - `AH = 0x02`: Display character in `DL`.
  - `AH = 0x09`: Display `$`-terminated string pointed to by `EDX`.
  - `AH = 0x4C`: Terminate process.
