; =============================================================================
; em836 16-Bit Real Mode COM1 Serial Port Demo (examples/16bit-com1_demo.as)
; Demonstrates:
;   - .use16 16-bit Real Mode (i286) execution
;   - UART COM1 Data Port (0x03F8) character transmission via OUT DX, AL
; =============================================================================

#include "syscalls.inc"

.use16
.org 0

.code
main:
    mov dx, 0x03F8              ; COM1 UART Data Port

    mov al, '1'
    out dx, al
    mov al, '6'
    out dx, al
    mov al, '-'
    out dx, al
    mov al, 'B'
    out dx, al
    mov al, 'i'
    out dx, al
    mov al, 't'
    out dx, al
    mov al, ' '
    out dx, al
    mov al, 'C'
    out dx, al
    mov al, 'O'
    out dx, al
    mov al, 'M'
    out dx, al
    mov al, '1'
    out dx, al
    mov al, '!'
    out dx, al
    mov al, 10                  ; '\n'
    out dx, al

    mov ax, SYS_EXIT
    mov bx, 0
    int 0x88
    hlt
