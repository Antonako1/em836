; =============================================================================
; em836 Console I/O Syscalls Demo (examples/console_io_demo.as)
; Demonstrates:
;   - SYS_PRINT_CHAR (0x10) : Output single ASCII character in BL
;   - SYS_PRINT_STR  (0x11) : Output null-terminated (ECX=0) & bounded strings
;   - SYS_READ_CHAR  (0x12) : Dequeue input character from Console FIFO queue
;   - SYS_WRITE      (0x22) : Write buffer to FD_STDOUT (1) / FD_STDERR (2)
;   - SYS_EXIT       (0x01) : Terminate guest program
; =============================================================================

#include "syscalls.inc"

.use32
.org 0

.code
main:
    ; 1. Print banner string (null-terminated, ECX = 0)
    mov eax, SYS_PRINT_STR
    mov ebx, msg_banner
    mov ecx, 0
    int 0x88

    ; 2. Print individual characters using SYS_PRINT_CHAR (0x10)
    mov eax, SYS_PRINT_CHAR
    mov ebx, 0x5B               ; '['
    int 0x88

    mov eax, SYS_PRINT_CHAR
    mov ebx, 'O'
    int 0x88

    mov eax, SYS_PRINT_CHAR
    mov ebx, 'K'
    int 0x88

    mov eax, SYS_PRINT_CHAR
    mov ebx, 0x5D               ; ']'
    int 0x88

    mov eax, SYS_PRINT_CHAR
    mov ebx, 10                 ; '\n'
    int 0x88

    ; 3. Print a bounded substring using SYS_PRINT_STR with ECX = 16
    mov eax, SYS_PRINT_STR
    mov ebx, msg_bounded
    mov ecx, 16                 ; Only print first 16 characters
    int 0x88

    mov eax, SYS_PRINT_CHAR
    mov ebx, 10                 ; '\n'
    int 0x88

    ; 4. Write to standard output (FD_STDOUT = 1) using SYS_WRITE (0x22)
    mov eax, SYS_WRITE
    mov ebx, FD_STDOUT
    mov ecx, msg_stdout
    mov edx, 38                 ; Exact byte count of msg_stdout (38 bytes)
    int 0x88

    ; 5. Check for a queued input character using SYS_READ_CHAR (0x12)
    mov eax, SYS_READ_CHAR
    mov ebx, 0                  ; Non-blocking poll
    int 0x88

    cmp eax, -1
    je no_input_queued

    ; Echo the character read from the input queue
    mov esi, eax
    mov eax, SYS_PRINT_STR
    mov ebx, msg_got_char
    mov ecx, 0
    int 0x88

    mov eax, SYS_PRINT_CHAR
    mov ebx, esi
    int 0x88

    mov eax, SYS_PRINT_CHAR
    mov ebx, 10
    int 0x88
    jmp done

no_input_queued:
    mov eax, SYS_PRINT_STR
    mov ebx, msg_no_char
    mov ecx, 0
    int 0x88

done:
    mov eax, SYS_EXIT
    mov ebx, 0
    int 0x88
    hlt

.rodata
msg_banner   db "=== em836 Console I/O Syscall Demo (0x10..0x12) ===\n", 0
msg_bounded  db "[Bounded String] THIS PART IS TRUNCATED BY ECX=16", 0
msg_stdout   db "[Write 0x22] Written to stdout fd (1)\n", 0
msg_got_char db "[ReadChar 0x12] Read queued character: ", 0
msg_no_char  db "[ReadChar 0x12] Input queue empty (returned -1).\n", 0
