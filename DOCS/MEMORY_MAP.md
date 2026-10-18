# Physical Memory Map & Video Hardware Specification — em836

**em836** maps a contiguous flat physical RAM buffer starting at `0x00000000` (16 MB by default, configurable from 4 MB to 64 MB in **File $\to$ Settings...**) into Unicorn Engine's guest address space with read, write, and execute permissions (`UC_PROT_ALL`).

---

## 1. Guest Physical Memory Layout

```text
0x00000000 +---------------------------------------------------------------+
           | Program Origin (.code, .data, .rodata)                        |
           | - Default binary load address & initial EIP = 0x00000000      |
0x000A0000 +---------------------------------------------------------------+
           | VGA 640x480 Graphics Linear Framebuffer (307,200 bytes)       |
           | - 1 byte per pixel (palette index 0x00 .. 0x0F)               |
           | - Ends at 0x000EAFF                                           |
0x000B8000 +---------------------------------------------------------------+
           | VGA 80x25 Text Mode Buffer (4,000 bytes)                      |
           | - 2 bytes per cell: [Byte 0: CP437 Char, Byte 1: Attribute]   |
           | - Ends at 0x000B8FA0                                          |
0x00100000 +---------------------------------------------------------------+
           | General Scratch RAM / User Data Buffers (1 MB - 2 MB)         |
0x00200000 +---------------------------------------------------------------+
           | Dynamic Heap Region (Managed via SYS_SBRK, INT 0x88 EAX=0x30) |
           | - Initial program break = 0x00200000 (2 MB)                   |
           | - Grows upward toward 0x00700000 (7 MB)                       |
0x00700000 +---------------------------------------------------------------+
           | Stack Growth Region (Grows downward from 0x00800000)          |
0x00800000 +---------------------------------------------------------------+
           | Default Initial Stack Pointer (ESP = 0x00800000, EBP = ESP)   |
0x01000000 +---------------------------------------------------------------+
           | End of Default 16 MB Physical RAM                             |
           +---------------------------------------------------------------+
```

| Start Address | End Address | Size | Region / Purpose |
| :--- | :--- | :--- | :--- |
| `0x00000000` | `0x0009FFFF` | 640 KB | Default code/data load region (`.org 0`) |
| `0x000A0000` | `0x000EAFFF` | 300 KB | VGA 640×480 Graphics Mode linear framebuffer (`307,200` bytes) |
| `0x000B8000` | `0x000B8F9F` | 4 KB | IBM PC 80×25 VGA Text Mode character/attribute buffer (`4,000` bytes) |
| `0x00200000` | `0x006FFFFF` | 5 MB | Dynamic Heap region managed by `SYS_SBRK` (`0x30`) |
| `0x00700000` | `0x00800000` | 1 MB | Default downward-growing call stack (`ESP` initialized to `0x00800000`) |
| `0x00800000` | `m_ramSize - 1` | 8+ MB | Extended upper guest RAM |

---

## 2. Dual-Mode VGA Display Subsystem

The `VgaSubsystem` renders guest video RAM to a 640×480 `SDL_Texture` streamed at 60 FPS into the **VGA Display** window. You can switch modes either via the UI toolbar or programmatically via [`SYS_GFX_SET_MODE`](SYSCALLS.md#sys_gfx_set_mode-0x40) (`INT 0x88`, `EAX = 0x40`) or `INT 0x10`.

### 2.1 Mode 0: 80×25 Text Mode (`0x000B8000`)
- **Grid Dimensions**: 80 columns $\times$ 25 rows = 2,000 character cells (`4,000` bytes total).
- **Glyph Rendering**: Each character is rendered using an embedded 8×16 IBM CP437 bitmap font (`80 * 8 = 640` px wide, `25 * 16 = 400` px tall, centered in the 640×480 viewport).
- **Cell Memory Layout**:
  $$\text{Address}(col, row) = \text{0x000B8000} + 2 \times (row \times 80 + col)$$
  - **Byte `+0`**: 8-bit CP437 ASCII character code.
  - **Byte `+1`**: Color attribute byte:
    - Bits `0..3` (low nibble): Foreground color (`0`–`15`).
    - Bits `4..7` (high nibble): Background color (`0`–`15`).

Example ([`examples/hello.as`](../examples/hello.as)):
```nasm
mov edi, 0x000B8000
mov byte [edi], 'H'         ; Character 'H'
mov byte [edi + 1], 0x1E    ; Attribute: Yellow (0xE) on Blue (0x1)
```

### 2.2 Mode 1: 640×480 Graphics Mode (`0x000A0000`)
- **Resolution**: 640 $\times$ 480 pixels (`307,200` bytes).
- **Pixel Encoding**: Flat 1-byte-per-pixel linear array; the lower 4 bits (`value & 0x0F`) select one of the 16 standard CGA/VGA colors.
- **Pixel Address Formula**:
  $$\text{Address}(x, y) = \text{0x000A0000} + (y \times 640) + x$$
- Guests may write directly to `0x000A0000` or invoke the hardware-accelerated [`INT 0x88` Graphics Syscalls (`0x40`–`0x44`)](SYSCALLS.md#35-accelerated-graphics-primitives-0x40--0x44).

---

## 3. Standard 16-Color CGA/VGA Palette

| Index (Dec) | Index (Hex) | Constant in `syscalls.inc` | Color Name | RGB Hex |
| :--- | :--- | :--- | :--- | :--- |
| `0` | `0x0` | `COLOR_BLACK` | Black | `#000000` |
| `1` | `0x1` | `COLOR_BLUE` | Blue | `#0000AA` |
| `2` | `0x2` | `COLOR_GREEN` | Green | `#00AA00` |
| `3` | `0x3` | `COLOR_CYAN` | Cyan | `#00AAAA` |
| `4` | `0x4` | `COLOR_RED` | Red | `#AA0000` |
| `5` | `0x5` | `COLOR_MAGENTA` | Magenta | `#AA00AA` |
| `6` | `0x6` | `COLOR_BROWN` | Brown | `#AA5500` |
| `7` | `0x7` | `COLOR_LIGHT_GRAY` | Light Gray | `#AAAAAA` |
| `8` | `0x8` | `COLOR_DARK_GRAY` | Dark Gray | `#555555` |
| `9` | `0x9` | `COLOR_LIGHT_BLUE` | Light Blue | `#5555FF` |
| `10` | `0xA` | `COLOR_LIGHT_GREEN` | Light Green | `#55FF55` |
| `11` | `0xB` | `COLOR_LIGHT_CYAN` | Light Cyan | `#55FFFF` |
| `12` | `0xC` | `COLOR_LIGHT_RED` | Light Red | `#FF5555` |
| `13` | `0xD` | `COLOR_LIGHT_MAGENTA` | Light Magenta | `#FF55FF` |
| `14` | `0xE` | `COLOR_YELLOW` | Yellow | `#FFFF55` |
| `15` | `0xF` | `COLOR_WHITE` | White | `#FFFFFF` |

---

## 4. Hardware I/O Ports (`IN` / `OUT`)

| Port | Device | Direction | Behavior |
| :--- | :--- | :--- | :--- |
| `0x03F8` | **COM1 UART Data** | `OUT` / `IN` | `OUT 0x03F8, AL` writes `AL` to Virtual Terminal; `IN AL, 0x03F8` returns `0x00`. |
| `0x00E9` | **Bochs Debug Port** | `OUT` | `OUT 0x00E9, AL` writes `AL` directly to Virtual Terminal. |

See [`DOCS/SYSCALLS.md`](SYSCALLS.md) for the complete `INT 0x88` and legacy interrupt specification.