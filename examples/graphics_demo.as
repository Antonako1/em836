; =============================================================================
; em836 Graphics Primitives Syscall Demo (examples/graphics_demo.as)
; Demonstrates:
;   - SYS_GFX_SET_MODE  (0x40) : Switch to 640x480 16-color graphics mode
;   - SYS_GFX_CLEAR     (0x41) : Clear framebuffer with a solid palette color
;   - SYS_GFX_FILL_RECT (0x44) : Draw filled rectangles
;   - SYS_GFX_LINE      (0x43) : Draw Bresenham lines
;   - SYS_GFX_PLOT      (0x42) : Plot individual pixels
;   - SYS_PRINT_STR     (0x11) : Log status message to Virtual Terminal
;   - SYS_EXIT          (0x01) : Cleanly halt execution
; =============================================================================

#include "syscalls.inc"

.use32
.org 0

.code
main:
    ; 1. Switch to 640x480 16-color Graphics Mode
    mov eax, SYS_GFX_SET_MODE
    mov ebx, VIDEO_MODE_GFX
    int 0x88

    ; 2. Clear screen to Blue (Color 1)
    mov eax, SYS_GFX_CLEAR
    mov ebx, COLOR_BLUE
    int 0x88

    ; 3. Draw a Cyan header banner rectangle (X=40, Y=30, W=560, H=80, Color=3)
    mov eax, SYS_GFX_FILL_RECT
    mov ebx, 40
    mov ecx, 30
    mov edx, 560
    mov esi, 80
    mov edi, COLOR_CYAN
    int 0x88

    ; 4. Draw a Dark Gray content panel (X=40, Y=130, W=560, H=300, Color=8)
    mov eax, SYS_GFX_FILL_RECT
    mov ebx, 40
    mov ecx, 130
    mov edx, 560
    mov esi, 300
    mov edi, COLOR_DARK_GRAY
    int 0x88

    ; 5. Draw intersecting diagonal lines across the header (Yellow, Color=14)
    mov eax, SYS_GFX_LINE
    mov ebx, 40
    mov ecx, 30
    mov edx, 600
    mov esi, 110
    mov edi, COLOR_YELLOW
    int 0x88

    mov eax, SYS_GFX_LINE
    mov ebx, 40
    mov ecx, 110
    mov edx, 600
    mov esi, 30
    mov edi, COLOR_LIGHT_RED
    int 0x88

    ; 6. Plot a horizontal dotted row of White pixels (SYS_GFX_PLOT) at Y=280
    mov esi, 80                 ; Starting X coordinate
plot_loop:
    mov eax, SYS_GFX_PLOT
    mov ebx, esi                ; X
    mov ecx, 280                ; Y
    mov edx, COLOR_WHITE        ; Color 15 (White)
    int 0x88

    add esi, 8
    cmp esi, 560
    jl plot_loop

    ; 7. Print completion string to the Virtual Terminal
    mov eax, SYS_PRINT_STR
    mov ebx, hello_msg
    mov ecx, 0                  ; 0 = null-terminated
    int 0x88

    ; 8. Exit cleanly with status code 0
    mov eax, SYS_EXIT
    mov ebx, 0
    int 0x88
    hlt

.rodata
hello_msg db "[Graphics Demo] Rendered 640x480 scene via INT 0x88 (0x40..0x44).\n", 0
