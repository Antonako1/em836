; =============================================================================
; em836 16-Bit Real Mode Console I/O Demo (examples/16bit-console_io_demo.as)
; Demonstrates:
;   - .use16 16-bit Real Mode (i286) execution
;   - SYS_PRINT_STR  (0x11) : Null-terminated (CX=0) and bounded string output
;   - SYS_PRINT_CHAR (0x10) : Single character output in BL
;   - SYS_WRITE      (0x22) : Standard output write (BX=1)
;   - SYS_READ_CHAR  (0x12) : Non-blocking input queue poll
; =============================================================================

#include "syscalls.inc"

.use16
.org 0

.code
main:
    ; 1. Print banner string (null-terminated, CX = 0)
    mov ax, SYS_PRINT_STR
    mov bx, msg_banner
    mov cx, 0
    int 0x88

    ; 2. Print "[16]" character-by-character using SYS_PRINT_CHAR (0x10)
    mov ax, SYS_PRINT_CHAR
    mov bx, 0x5B                ; '['
    int 0x88

    mov ax, SYS_PRINT_CHAR
    mov bx, '1'
    int 0x88

    mov ax, SYS_PRINT_CHAR
    mov bx, '6'
    int 0x88

    mov ax, SYS_PRINT_CHAR
    mov bx, 0x5D                ; ']'
    int 0x88

    mov ax, SYS_PRINT_CHAR
    mov bx, 10                  ; '\n'
    int 0x88

    ; 3. Print bounded string (first 18 bytes) via SYS_PRINT_STR
    mov ax, SYS_PRINT_STR
    mov bx, msg_bounded
    mov cx, 18
    int 0x88

    mov ax, SYS_PRINT_CHAR
    mov bx, 10                  ; '\n'
    int 0x88

    ; 4. Write to stdout (FD = 1) using SYS_WRITE (0x22)
    mov ax, SYS_WRITE
    mov bx, FD_STDOUT
    mov cx, msg_stdout
    mov dx, 41                  ; Exact length of msg_stdout (41 bytes)
    int 0x88

    ; 5. Poll guest input queue via SYS_READ_CHAR (0x12)
    mov ax, SYS_READ_CHAR
    mov bx, 0
    int 0x88

    cmp ax, 0xFFFF
    je no_input_16

    mov si, ax
    mov ax, SYS_PRINT_STR
    mov bx, msg_got_char
    mov cx, 0
    int 0x88

    mov ax, SYS_PRINT_CHAR
    mov bx, si
    int 0x88

    mov ax, SYS_PRINT_CHAR
    mov bx, 10
    int 0x88
    jmp exit_16

no_input_16:
    mov ax, SYS_PRINT_STR
    mov bx, msg_no_char
    mov cx, 0
    int 0x88

exit_16:
    mov ax, SYS_EXIT
    mov bx, 0
    int 0x88
    hlt

.rodata
msg_banner   db "=== em836 16-Bit Console I/O Demo (i286) ===\n", 0
msg_bounded  db "[16-Bit Substring] THIS TAIL IS TRUNCATED", 0
msg_stdout   db "[16-Bit Write] Written to stdout fd (1)!\n", 0
msg_got_char db "[16-Bit ReadChar] Dequeued char: ", 0
msg_no_char  db "[16-Bit ReadChar] Input queue empty (-1).\n", 0
