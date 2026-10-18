; =============================================================================
; em836 16-Bit Real Mode Legacy Interrupts Demo (examples/16bit-legacy_interrupts_demo.as)
; Demonstrates classic 16-bit BIOS, DOS, Linux, and I/O port calls:
;   - INT 0x10 (AH=0x00, AL=0x03) : BIOS Set 80x25 Text Mode
;   - INT 0x10 (AH=0x0E)          : BIOS Teletype Character Output
;   - INT 0x21 (AH=0x09)          : MS-DOS '$'-Terminated String Output (DX)
;   - INT 0x21 (AH=0x02)          : MS-DOS Character Output (DL)
;   - Port 0x00E9                 : Bochs Debug Port I/O
;   - INT 0x21 (AH=0x4C)          : MS-DOS Terminate Process
; =============================================================================

.use16
.org 0

.code
main:
    ; 1. BIOS INT 0x10 (AH=0x00, AL=0x03): Set 80x25 Text Mode
    mov ax, 0x0003
    int 0x10

    ; 2. BIOS INT 0x10 (AH=0x0E): Teletype output "[16-BIOS]\n"
    mov ax, 0x0E5B              ; '['
    int 0x10
    mov ax, 0x0E31              ; '1'
    int 0x10
    mov ax, 0x0E36              ; '6'
    int 0x10
    mov ax, 0x0E2D              ; '-'
    int 0x10
    mov ax, 0x0E42              ; 'B'
    int 0x10
    mov ax, 0x0E49              ; 'I'
    int 0x10
    mov ax, 0x0E4F              ; 'O'
    int 0x10
    mov ax, 0x0E53              ; 'S'
    int 0x10
    mov ax, 0x0E5D              ; ']'
    int 0x10
    mov ax, 0x0E0A              ; '\n'
    int 0x10

    ; 3. MS-DOS INT 0x21 (AH=0x09): Print '$'-terminated string at DX
    mov ax, 0x0900
    mov dx, dos_msg_16
    int 0x21

    ; 4. MS-DOS INT 0x21 (AH=0x02): Print newline in DL
    mov ax, 0x0200
    mov dx, 10
    int 0x21

    ; 5. Bochs Debug Port 0x00E9: Write "E9-16\n"
    mov dx, 0x00E9
    mov al, 'E'
    out dx, al
    mov al, '9'
    out dx, al
    mov al, '-'
    out dx, al
    mov al, '1'
    out dx, al
    mov al, '6'
    out dx, al
    mov al, 10
    out dx, al

    ; 6. MS-DOS INT 0x21 (AH=0x4C): Terminate Process
    mov ax, 0x4C00
    int 0x21
    hlt

.rodata
dos_msg_16 db "[16-Bit DOS INT 0x21] Real Mode string printed via AH=0x09!$"
