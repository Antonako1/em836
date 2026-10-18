; =============================================================================
; em836 Legacy Interrupts & Hardware I/O Ports Demo (examples/legacy_interrupts_demo.as)
; Demonstrates backward-compatible interrupts and debug ports:
;   - INT 0x10 (AH=0x00, AL=0x03) : BIOS Set 80x25 Text Mode
;   - INT 0x10 (AH=0x0E)          : BIOS Teletype Character Output
;   - INT 0x21 (AH=0x02)          : DOS Character Output (DL)
;   - INT 0x21 (AH=0x09)          : DOS '$'-Terminated String Output (EDX)
;   - INT 0x80 (EAX=4)            : Linux i386 sys_write (EBX=1 stdout)
;   - Port 0x03F8 / Port 0x00E9   : COM1 UART & Bochs E9 Debug Port I/O
;   - INT 0x80 (EAX=1)            : Linux i386 sys_exit (EBX=0)
; =============================================================================

.use32
.org 0

.code
main:
    ; 1. BIOS INT 0x10 (AH=0x00, AL=0x03): Switch to 80x25 Text Mode
    mov eax, 0x0003
    int 0x10

    ; 2. BIOS INT 0x10 (AH=0x0E): Teletype output "[BIOS] "
    mov eax, 0x0E5B             ; '['
    int 0x10
    mov eax, 0x0E42             ; 'B'
    int 0x10
    mov eax, 0x0E49             ; 'I'
    int 0x10
    mov eax, 0x0E4F             ; 'O'
    int 0x10
    mov eax, 0x0E53             ; 'S'
    int 0x10
    mov eax, 0x0E5D             ; ']'
    int 0x10
    mov eax, 0x0E0A             ; '\n'
    int 0x10

    ; 3. DOS INT 0x21 (AH=0x09): Print '$'-terminated string at EDX
    mov eax, 0x0900
    mov edx, dos_msg
    int 0x21

    ; 4. DOS INT 0x21 (AH=0x02): Print single char in DL ('\n')
    mov eax, 0x0200
    mov edx, 10
    int 0x21

    ; 5. Linux INT 0x80 (EAX=4, sys_write): Write 34 bytes to stdout (EBX=1)
    mov eax, 4                  ; sys_write
    mov ebx, 1                  ; fd = 1 (stdout)
    mov ecx, linux_msg          ; buffer address
    mov edx, 34                 ; byte count
    int 0x80

    ; 6. Bochs Debug Port 0x00E9: Write "E9\n" via OUT DX, AL
    mov dx, 0x00E9
    mov al, 'E'
    out dx, al
    mov al, '9'
    out dx, al
    mov al, 10
    out dx, al

    ; 7. Linux INT 0x80 (EAX=1, sys_exit): Terminate with code 0
    mov eax, 1                  ; sys_exit
    mov ebx, 0                  ; status = 0
    int 0x80
    hlt

.rodata
dos_msg   db "[DOS INT 0x21] Dollar-terminated string printed!$"
linux_msg db "[Linux INT 0x80] sys_write stdout\n", 0
