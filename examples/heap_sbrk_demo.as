; =============================================================================
; em836 Dynamic Heap Allocation Syscall Demo (examples/heap_sbrk_demo.as)
; Demonstrates:
;   - SYS_SBRK      (0x30) : Query and increment program break (heap starts at 2 MB)
;   - SYS_PRINT_STR (0x11) : Print dynamically populated heap buffer
;   - SYS_EXIT      (0x01) : Halt execution
; =============================================================================

#include "syscalls.inc"

.use32
.org 0

.code
main:
    mov eax, SYS_PRINT_STR
    mov ebx, msg_header
    mov ecx, 0
    int 0x88

    ; 1. Query initial program break (EBX = 0 -> returns 0x00200000 in EAX)
    mov eax, SYS_SBRK
    mov ebx, 0
    int 0x88

    ; 2. Allocate 64 bytes on the guest heap (EBX = 64)
    mov eax, SYS_SBRK
    mov ebx, 64
    int 0x88

    cmp eax, -1
    je oom_error
    mov edi, eax                ; EDI = pointer to newly allocated 64-byte block (0x00200000)

    ; 3. Write "HEAP_OK!\n\0" directly into the dynamically allocated heap block
    mov byte [edi + 0], 'H'
    mov byte [edi + 1], 'E'
    mov byte [edi + 2], 'A'
    mov byte [edi + 3], 'P'
    mov byte [edi + 4], '_'
    mov byte [edi + 5], 'O'
    mov byte [edi + 6], 'K'
    mov byte [edi + 7], '!'
    mov byte [edi + 8], 10      ; '\n'
    mov byte [edi + 9], 0       ; Null terminator

    ; 4. Print prefix and then print the string directly from the heap pointer (EDI)
    mov eax, SYS_PRINT_STR
    mov ebx, msg_allocated
    mov ecx, 0
    int 0x88

    mov eax, SYS_PRINT_STR
    mov ebx, edi                ; Heap address 0x00200000
    mov ecx, 0
    int 0x88

    ; 5. Allocate a second 128-byte block on the guest heap
    mov eax, SYS_SBRK
    mov ebx, 128
    int 0x88
    mov esi, eax                ; ESI = 0x00200040

    ; 6. Query updated program break (EBX = 0 -> 0x002000C0)
    mov eax, SYS_SBRK
    mov ebx, 0
    int 0x88

    mov eax, SYS_PRINT_STR
    mov ebx, msg_done
    mov ecx, 0
    int 0x88

    mov eax, SYS_EXIT
    mov ebx, 0
    int 0x88
    hlt

oom_error:
    mov eax, SYS_PRINT_STR
    mov ebx, msg_oom
    mov ecx, 0
    int 0x88

    mov eax, SYS_EXIT
    mov ebx, 1
    int 0x88
    hlt

.rodata
msg_header    db "=== em836 Dynamic Memory (SYS_SBRK 0x30) Demo ===\n", 0
msg_allocated db "[SYS_SBRK] Allocated 64B at 0x00200000. Contents: ", 0
msg_done      db "[SYS_SBRK] Second 128B block allocated at 0x00200040 (new break = 0x002000C0).\n", 0
msg_oom       db "[SYS_SBRK] Out of heap memory!\n", 0
