; =============================================================================
; em836 16-Bit Real Mode Process & Timer Demo (examples/16bit-process_ticks_demo.as)
; Demonstrates:
;   - .use16 16-bit Real Mode (i286) execution
;   - SYS_GET_TICKS (0x03) : Read low 16 bits of host elapsed milliseconds in AX
;   - SYS_YIELD     (0x02) : Yield CPU execution quantum
;   - SYS_EXIT      (0x01) : Terminate guest execution
; =============================================================================

#include "syscalls.inc"

.use16
.org 0

.code
main:
    mov ax, SYS_PRINT_STR
    mov bx, msg_start
    mov cx, 0
    int 0x88

    ; 1. Sample start ticks in AX
    mov ax, SYS_GET_TICKS
    int 0x88
    mov di, ax

    ; 2. Yield 5 times and print '.' each iteration
    mov si, 5
yield_16:
    mov ax, SYS_YIELD
    int 0x88

    mov ax, SYS_PRINT_CHAR
    mov bx, 0x2E                ; '.'
    int 0x88

    dec si
    jnz yield_16

    mov ax, SYS_PRINT_CHAR
    mov bx, 10                  ; '\n'
    int 0x88

    ; 3. Sample end ticks and compute delta
    mov ax, SYS_GET_TICKS
    int 0x88
    sub ax, di
    mov si, ax

    mov ax, SYS_PRINT_STR
    mov bx, msg_done
    mov cx, 0
    int 0x88

    mov ax, si
    and ax, 0x07
    add ax, '0'
    mov bx, ax
    mov ax, SYS_PRINT_CHAR
    int 0x88

    mov ax, SYS_PRINT_STR
    mov bx, msg_ms
    mov cx, 0
    int 0x88

    mov ax, SYS_EXIT
    mov bx, 0
    int 0x88
    hlt

.rodata
msg_start db "[16-Bit Process Demo] Sampling GetTicks (0x03) & Yield (0x02)", 0
msg_done  db "[16-Bit Process Demo] Completed 5 yields. Delta tick bucket: ", 0
msg_ms    db " ms.\n", 0
