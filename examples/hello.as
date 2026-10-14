; =============================================================
; em836 VGA Text Mode Demo (0xB8000 Direct Buffer Writes)
; Writes 'H', 'E', 'L', 'L', 'O' in Yellow on Blue to VGA RAM
; =============================================================
.use32
.org 0
.code
main:
    ; Ensure VGA Text Mode (Mode 0)
    mov eax, 0x40           ; SYS_GFX_SET_MODE
    mov ebx, 0              ; 0 = Text 80x25
    int 0x88

    mov edi, 0x000B8000     ; Base address of 80x25 Text Mode Buffer

    ; Character 'H' (0x48), Attribute 0x1E (Yellow on Blue)
    mov byte [edi], 0x48
    mov byte [edi + 1], 0x1E

    ; Character 'E' (0x45)
    mov byte [edi + 2], 0x45
    mov byte [edi + 3], 0x1E

    ; Character 'L' (0x4C)
    mov byte [edi + 4], 0x4C
    mov byte [edi + 5], 0x1E

    ; Character 'L' (0x4C)
    mov byte [edi + 6], 0x4C
    mov byte [edi + 7], 0x1E

    ; Character 'O' (0x4F)
    mov byte [edi + 8], 0x4F
    mov byte [edi + 9], 0x1E

    ; Character ' ' (0x20)
    mov byte [edi + 10], 0x20
    mov byte [edi + 11], 0x1E

    ; Character 'e' (0x6D)
    mov byte [edi + 12], 0x6D
    mov byte [edi + 13], 0x1A   ; Bright Green on Blue

    ; Character 'm' (0x38)
    mov byte [edi + 14], 0x38
    mov byte [edi + 15], 0x1A

    ; Character '8' (0x33)
    mov byte [edi + 16], 0x33
    mov byte [edi + 17], 0x1A

    ; Character '6' (0x36)
    mov byte [edi + 18], 0x36
    mov byte [edi + 19], 0x1A

    ; Print confirmation message to Console
    mov eax, 0x11           ; SYS_PRINT_STR
    mov ebx, console_msg
    mov ecx, 0
    int 0x88

    ; Exit
    mov eax, 0x01           ; SYS_EXIT
    mov ebx, 0
    int 0x88

.rodata
console_msg:
    db "[VGA Demo] 'HELLO em836' written to 0xB8000.", 0
