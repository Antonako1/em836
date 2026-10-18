; =============================================================================
; em836 16-Bit Real Mode VGA Text Mode Demo (examples/16bit-hello.as)
; Demonstrates:
;   - .use16 16-bit Real Mode (i286) execution
;   - Segmented memory addressing: ES = 0xB800 -> physical 0x000B8000
;   - Direct character/attribute writes via [es:di]
;   - 16-bit INT 0x88 syscalls (SYS_GFX_SET_MODE, SYS_PRINT_STR, SYS_EXIT)
; =============================================================================

#include "syscalls.inc"

.use16
.org 0

.code
main:
    ; 1. Switch VGA display to 80x25 Text Mode (Mode 0)
    mov ax, SYS_GFX_SET_MODE
    mov bx, VIDEO_MODE_TEXT
    int 0x88

    ; 2. Point ES segment register to 0xB800 (physical address 0x000B8000)
    mov ax, 0xB800
    mov es, ax
    mov di, 0

    ; Write "16-BIT REAL MODE" in Bright Cyan on Blue (0x1B) at Row 0, Col 0
    mov byte [es:di + 0],  '1'
    mov byte [es:di + 1],  0x1B
    mov byte [es:di + 2],  '6'
    mov byte [es:di + 3],  0x1B
    mov byte [es:di + 4],  '-'
    mov byte [es:di + 5],  0x1B
    mov byte [es:di + 6],  'B'
    mov byte [es:di + 7],  0x1B
    mov byte [es:di + 8],  'I'
    mov byte [es:di + 9],  0x1B
    mov byte [es:di + 10], 'T'
    mov byte [es:di + 11], 0x1B
    mov byte [es:di + 12], ' '
    mov byte [es:di + 13], 0x1B
    mov byte [es:di + 14], 'i'
    mov byte [es:di + 15], 0x1E
    mov byte [es:di + 16], '2'
    mov byte [es:di + 17], 0x1E
    mov byte [es:di + 18], '8'
    mov byte [es:di + 19], 0x1E
    mov byte [es:di + 20], '6'
    mov byte [es:di + 21], 0x1E

    ; 3. Print confirmation string to Virtual Terminal
    mov ax, SYS_PRINT_STR
    mov bx, msg_ok
    mov cx, 0
    int 0x88

    ; 4. Exit cleanly
    mov ax, SYS_EXIT
    mov bx, 0
    int 0x88
    hlt

.rodata
msg_ok db "[16-bit VGA Demo] Wrote '16-BIT i286' to segment 0xB800:0x0000.\n", 0
