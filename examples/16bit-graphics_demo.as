; =============================================================================
; em836 16-Bit Real Mode VGA Graphics Demo (examples/16bit-graphics_demo.as)
; Demonstrates:
;   - .use16 16-bit Real Mode (i286) execution
;   - 16-bit register ABI (AX, BX, CX, DX, SI, DI) for INT 0x88 graphics:
;     * SYS_GFX_SET_MODE  (0x40)
;     * SYS_GFX_CLEAR     (0x41)
;     * SYS_GFX_PLOT      (0x42)
;     * SYS_GFX_LINE      (0x43)
;     * SYS_GFX_FILL_RECT (0x44)
; =============================================================================

#include "syscalls.inc"

.use16
.org 0

.code
main:
    ; 1. Switch to 640x480 Graphics Mode (BX = 1)
    mov ax, SYS_GFX_SET_MODE
    mov bx, VIDEO_MODE_GFX
    int 0x88

    ; 2. Clear screen to Black (BX = 0)
    mov ax, SYS_GFX_CLEAR
    mov bx, COLOR_BLACK
    int 0x88

    ; 3. Fill outer banner frame (X=40, Y=30, W=560, H=80, Color=Magenta)
    mov ax, SYS_GFX_FILL_RECT
    mov bx, 40
    mov cx, 30
    mov dx, 560
    mov si, 80
    mov di, COLOR_MAGENTA
    int 0x88

    ; 4. Fill inner canvas (X=40, Y=130, W=560, H=300, Color=Blue)
    mov ax, SYS_GFX_FILL_RECT
    mov bx, 40
    mov cx, 130
    mov dx, 560
    mov si, 300
    mov di, COLOR_BLUE
    int 0x88

    ; 5. Draw diagonal criss-cross lines across inner canvas
    mov ax, SYS_GFX_LINE
    mov bx, 40
    mov cx, 130
    mov dx, 600
    mov si, 430
    mov di, COLOR_YELLOW
    int 0x88

    mov ax, SYS_GFX_LINE
    mov bx, 40
    mov cx, 430
    mov dx, 600
    mov si, 130
    mov di, COLOR_LIGHT_CYAN
    int 0x88

    ; 6. Plot a horizontal row of white pixels across the middle (Y = 280)
    mov si, 60
plot_loop_16:
    mov ax, SYS_GFX_PLOT
    mov bx, si
    mov cx, 280
    mov dx, COLOR_WHITE
    int 0x88

    add si, 10
    cmp si, 580
    jl plot_loop_16

    ; 7. Log completion to Virtual Terminal
    mov ax, SYS_PRINT_STR
    mov bx, msg_done
    mov cx, 0
    int 0x88

    ; 8. Halt
    mov ax, SYS_EXIT
    mov bx, 0
    int 0x88
    hlt

.rodata
msg_done db "[16-bit Graphics Demo] Rendered 640x480 scene in 16-bit Real Mode (i286).\n", 0
