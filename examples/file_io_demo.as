; =============================================================================
; em836 Host File I/O Syscalls Demo (examples/file_io_demo.as)
; Demonstrates:
;   - SYS_OPEN  (0x20) : Open host file (0=Read, 1=Write, 2=Append)
;   - SYS_WRITE (0x22) : Write guest buffer to open file handle
;   - SYS_CLOSE (0x23) : Close open file handle
;   - SYS_SEEK  (0x24) : Reposition file pointer (0=SEEK_SET, 1=CUR, 2=END)
;   - SYS_READ  (0x21) : Read bytes from open file into guest RAM buffer
; =============================================================================

#include "syscalls.inc"

#define READ_BUFFER_ADDR 0x00100000

.use32
.org 0

.code
main:
    mov eax, SYS_PRINT_STR
    mov ebx, msg_header
    mov ecx, 0
    int 0x88

    ; 1. Open "em836_file_test.txt" for writing (ECX = O_WRONLY = 1)
    mov eax, SYS_OPEN
    mov ebx, filename
    mov ecx, O_WRONLY
    int 0x88

    cmp eax, -1
    je io_error
    mov esi, eax                ; ESI = file handle

    ; 2. Write 27 bytes of file_payload to file handle in ESI
    mov eax, SYS_WRITE
    mov ebx, esi
    mov ecx, file_payload
    mov edx, 27
    int 0x88

    ; 3. Close the output file
    mov eax, SYS_CLOSE
    mov ebx, esi
    int 0x88

    mov eax, SYS_PRINT_STR
    mov ebx, msg_written
    mov ecx, 0
    int 0x88

    ; 4. Re-open "em836_file_test.txt" for reading (ECX = O_RDONLY = 0)
    mov eax, SYS_OPEN
    mov ebx, filename
    mov ecx, O_RDONLY
    int 0x88

    cmp eax, -1
    je io_error
    mov esi, eax                ; ESI = file handle

    ; 5. Seek to byte offset 6 ("from em836 File I/O!\n") via SYS_SEEK (0x24)
    mov eax, SYS_SEEK
    mov ebx, esi                ; File handle
    mov ecx, 6                  ; Offset = 6 bytes
    mov edx, SEEK_SET           ; Origin = SEEK_SET (0)
    int 0x88

    ; 6. Read remaining 21 bytes into guest RAM at READ_BUFFER_ADDR (0x00100000)
    mov eax, SYS_READ
    mov ebx, esi                ; File handle
    mov ecx, READ_BUFFER_ADDR   ; Destination buffer in guest RAM
    mov edx, 21                 ; Byte count to read
    int 0x88

    ; Null-terminate the buffer at READ_BUFFER_ADDR + bytes_read
    mov edi, READ_BUFFER_ADDR
    add edi, eax
    mov byte [edi], 0

    ; 7. Close the input file
    mov eax, SYS_CLOSE
    mov ebx, esi
    int 0x88

    ; 8. Print the seeked & read string to the Virtual Terminal
    mov eax, SYS_PRINT_STR
    mov ebx, msg_read_prefix
    mov ecx, 0
    int 0x88

    mov eax, SYS_PRINT_STR
    mov ebx, READ_BUFFER_ADDR
    mov ecx, 0
    int 0x88

    ; 9. Clean exit
    mov eax, SYS_EXIT
    mov ebx, 0
    int 0x88
    hlt

io_error:
    mov eax, SYS_PRINT_STR
    mov ebx, msg_err
    mov ecx, 0
    int 0x88

    mov eax, SYS_EXIT
    mov ebx, 1
    int 0x88
    hlt

.rodata
filename        db "em836_file_test.txt", 0
file_payload    db "Hello from em836 File I/O!\n", 0
msg_header      db "=== em836 Host File I/O Demo (0x20..0x24) ===\n", 0
msg_written     db "[File I/O] Wrote 27 bytes to 'em836_file_test.txt'.\n", 0
msg_read_prefix db "[File I/O] Read after SYS_SEEK(offset=6): ", 0
msg_err         db "[File I/O Error] Operation failed.\n", 0
