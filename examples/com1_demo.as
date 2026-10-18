; =============================================================
; em836 COM1 Serial Port I/O Demo
; Writes characters to UART Data Port (0x03F8)
; =============================================================
.code
main:
    mov dx, 0x03F8          ; COM1 UART Port
    mov al, 0x48            ; 'H'
    out dx, al
    mov al, 0x65            ; 'e'
    out dx, al
    mov al, 0x6C            ; 'l'
    out dx, al
    mov al, 0x6C            ; 'l'
    out dx, al
    mov al, 0x6F            ; 'o'
    out dx, al
    mov al, 0x21            ; '!'
    out dx, al
    mov al, 0x0A            ; Newline
    out dx, al

    mov eax, 0x01           ; SYS_EXIT
    mov ebx, 0
    int 0x88
    hlt
