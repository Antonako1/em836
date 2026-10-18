; =============================================================================
; em836 Process Control & Timer Syscalls Demo (examples/process_ticks_demo.as)
; Demonstrates:
;   - SYS_GET_TICKS (0x03) : Query elapsed host milliseconds in EAX
;   - SYS_YIELD     (0x02) : Yield remaining CPU execution quantum
;   - SYS_EXIT      (0x01) : Halt execution with exit code in EBX
; =============================================================================

#include "syscalls.inc"

.use32
.org 0

.code
main:
    mov eax, SYS_PRINT_STR
    mov ebx, msg_start
    mov ecx, 0
    int 0x88

    ; 1. Read start timestamp (milliseconds) via SYS_GET_TICKS (0x03)
    mov eax, SYS_GET_TICKS
    int 0x88
    mov edi, eax                ; Save start_ticks in EDI

    ; 2. Run a work loop that yields the CPU 5 times via SYS_YIELD (0x02)
    mov esi, 5
yield_loop:
    mov eax, SYS_YIELD
    int 0x88

    mov eax, SYS_PRINT_CHAR
    mov ebx, '.'
    int 0x88

    dec esi
    jnz yield_loop

    mov eax, SYS_PRINT_CHAR
    mov ebx, 10                 ; '\n'
    int 0x88

    ; 3. Read end timestamp via SYS_GET_TICKS (0x03)
    mov eax, SYS_GET_TICKS
    int 0x88
    sub eax, edi                ; EAX = elapsed milliseconds (end - start)
    mov esi, eax

    ; 4. Print completion message
    mov eax, SYS_PRINT_STR
    mov ebx, msg_done
    mov ecx, 0
    int 0x88

    ; Print single-digit or low digit of elapsed ticks + '0'
    mov eax, esi
    and eax, 0x07
    add eax, '0'
    mov ebx, eax
    mov eax, SYS_PRINT_CHAR
    int 0x88

    mov eax, SYS_PRINT_STR
    mov ebx, msg_ms
    mov ecx, 0
    int 0x88

    ; 5. Exit cleanly with status code 0 via SYS_EXIT (0x01)
    mov eax, SYS_EXIT
    mov ebx, 0
    int 0x88
    hlt

.rodata
msg_start db "[Process Demo] Sampling SYS_GET_TICKS (0x03) and calling SYS_YIELD (0x02)", 0
msg_done  db "[Process Demo] Completed 5 yields. Delta tick bucket: ", 0
msg_ms    db " ms.\n", 0
