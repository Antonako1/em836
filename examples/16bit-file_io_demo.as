; =============================================================================
; em836 16-Bit Real Mode Host File I/O Demo (examples/16bit-file_io_demo.as)
; Demonstrates:
;   - .use16 16-bit Real Mode (i286) execution
;   - SYS_OPEN  (0x20), SYS_WRITE (0x22), SYS_CLOSE (0x23),
;     SYS_SEEK  (0x24), SYS_READ  (0x21) using 16-bit registers
; =============================================================================

#include "syscalls.inc"

#define BUF_ADDR_16 0x4000

.use16
.org 0

.code
main:
    mov ax, SYS_PRINT_STR
    mov bx, msg_header
    mov cx, 0
    int 0x88

    ; 1. Open "em836_16bit_test.txt" for writing (CX = O_WRONLY = 1)
    mov ax, SYS_OPEN
    mov bx, filename
    mov cx, O_WRONLY
    int 0x88

    cmp ax, 0xFFFF
    je io_err_16
    mov si, ax                  ; SI = file handle

    ; 2. Write 28 bytes of payload
    mov ax, SYS_WRITE
    mov bx, si
    mov cx, payload
    mov dx, 28
    int 0x88

    ; 3. Close output file
    mov ax, SYS_CLOSE
    mov bx, si
    int 0x88

    mov ax, SYS_PRINT_STR
    mov bx, msg_wrote
    mov cx, 0
    int 0x88

    ; 4. Re-open "em836_16bit_test.txt" for reading (CX = O_RDONLY = 0)
    mov ax, SYS_OPEN
    mov bx, filename
    mov cx, O_RDONLY
    int 0x88

    cmp ax, 0xFFFF
    je io_err_16
    mov si, ax

    ; 5. Seek to byte offset 7 ("16-Bit File I/O OK!\n")
    mov ax, SYS_SEEK
    mov bx, si
    mov cx, 7
    mov dx, SEEK_SET
    int 0x88

    ; 6. Read 21 bytes into BUF_ADDR_16 (0x4000)
    mov ax, SYS_READ
    mov bx, si
    mov cx, BUF_ADDR_16
    mov dx, 21
    int 0x88

    ; Null-terminate read buffer
    mov di, BUF_ADDR_16
    add di, ax
    mov byte [di], 0

    ; 7. Close input file
    mov ax, SYS_CLOSE
    mov bx, si
    int 0x88

    ; 8. Print seeked string
    mov ax, SYS_PRINT_STR
    mov bx, msg_read
    mov cx, 0
    int 0x88

    mov ax, SYS_PRINT_STR
    mov bx, BUF_ADDR_16
    mov cx, 0
    int 0x88

    mov ax, SYS_EXIT
    mov bx, 0
    int 0x88
    hlt

io_err_16:
    mov ax, SYS_PRINT_STR
    mov bx, msg_err
    mov cx, 0
    int 0x88

    mov ax, SYS_EXIT
    mov bx, 1
    int 0x88
    hlt

.rodata
filename   db "em836_16bit_test.txt", 0
payload    db " Header:16-Bit File I/O OK!\n", 0
msg_header db "=== em836 16-Bit Host File I/O Demo (i286) ===\n", 0
msg_wrote  db "[16-Bit File I/O] Wrote 28 bytes to 'em836_16bit_test.txt'.\n", 0
msg_read   db "[16-Bit File I/O] Read after Seek(7): ", 0
msg_err    db "[16-Bit File I/O] Error opening file.\n", 0
