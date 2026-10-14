; =============================================================
; em836 VGA 640x480 Graphics Demo
; Demonstrates custom INT 0x88 unified system call interface
; =============================================================

start:
    ; 1. Set display to 640x480 16-color Graphics Framebuffer (Mode 1)
    mov eax, 0x40           ; SYS_GFX_SET_MODE
    mov ebx, 1              ; 1 = Graphics 640x480
    int 0x88

    ; 2. Clear screen to Navy Blue (Color 1)
    mov eax, 0x41           ; SYS_GFX_CLEAR
    mov ebx, 1              ; Color 1
    int 0x88

    ; 3. Draw a Red header banner (Color 4)
    mov eax, 0x44           ; SYS_GFX_FILL_RECT
    mov ebx, 40             ; X = 40
    mov ecx, 20             ; Y = 20
    mov edx, 560            ; Width = 560
    mov esi, 60             ; Height = 60
    mov edi, 4              ; Color 4 (Red)
    int 0x88

    ; 4. Draw a Green center card (Color 2)
    mov eax, 0x44           ; SYS_GFX_FILL_RECT
    mov ebx, 120            ; X = 120
    mov ecx, 120            ; Y = 120
    mov edx, 400            ; Width = 400
    mov esi, 240            ; Height = 240
    mov edi, 2              ; Color 2 (Green)
    int 0x88

    ; 5. Draw a Yellow diagonal cross line (Color 14)
    mov eax, 0x43           ; SYS_GFX_LINE
    mov ebx, 120            ; X0
    mov ecx, 120            ; Y0
    mov edx, 520            ; X1
    mov esi, 360            ; Y1
    mov edi, 14             ; Color 14 (Yellow)
    int 0x88

    ; 6. Draw a Bright Cyan opposing diagonal line (Color 11)
    mov eax, 0x43           ; SYS_GFX_LINE
    mov ebx, 520            ; X0
    mov ecx, 120            ; Y0
    mov edx, 120            ; X1
    mov esi, 360            ; Y1
    mov edi, 11             ; Color 11 (Cyan)
    int 0x88

    ; 7. Output completion notice to Virtual Console Terminal
    mov eax, 0x11           ; SYS_PRINT_STR
    mov ebx, complete_msg
    mov ecx, 0
    int 0x88

    ; 8. Terminate process cleanly
    mov eax, 0x01           ; SYS_EXIT
    mov ebx, 0
    int 0x88

complete_msg:
    db "[Graphics Demo] Rendered test patterns at 640x480 resolution.", 0
