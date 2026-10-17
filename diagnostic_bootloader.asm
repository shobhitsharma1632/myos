org 0x7C00
bits 16

; ============================================================================
; LBA Support Diagnostic Bootloader - Simplified
; ============================================================================

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [drive_num], dl

    ; Print header
    mov al, 'D'
    call print_char
    mov al, 'R'
    call print_char
    mov al, 'V'
    call print_char
    mov al, ':'
    call print_char
    mov al, [drive_num]
    call print_hex_byte
    mov al, 0x0D
    call print_char
    mov al, 0x0A
    call print_char

    ; === Test 1: LBA Support ===
    mov al, 'T'
    call print_char
    mov al, 'L'
    call print_char
    mov al, ':'
    call print_char

    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [drive_num]
    stc
    int 13h

    jnc .lba_supported

    ; LBA not supported
    mov al, 'N'
    call print_char
    mov al, 'O'
    call print_char
    mov al, '/'
    call print_char
    mov al, ah
    call print_hex_byte
    mov al, 0x0D
    call print_char
    mov al, 0x0A
    call print_char
    jmp .chs_test

.lba_supported:
    cmp bx, 0xAA55
    jne .lba_bad_sig

    ; LBA supported!
    mov al, 'Y'
    call print_char
    mov al, 'E'
    call print_char
    mov al, 'S'
    call print_char
    mov al, 0x0D
    call print_char
    mov al, 0x0A
    call print_char
    jmp .chs_test

.lba_bad_sig:
    mov al, 'B'
    call print_char
    mov al, 'A'
    call print_char
    mov al, 'D'
    call print_char
    mov al, 0x0D
    call print_char
    mov al, 0x0A
    call print_char

    ; === Test 2: CHS Support ===
.chs_test:
    mov al, 'G'
    call print_char
    mov al, 'E'
    call print_char
    mov al, 'T'
    call print_char
    mov al, ':'
    call print_char

    mov ah, 0x08
    mov dl, [drive_num]
    xor di, di
    mov es, di
    stc
    int 13h

    jnc .chs_ok

    mov al, 'E'
    call print_char
    mov al, ah
    call print_hex_byte
    mov al, 0x0D
    call print_char
    mov al, 0x0A
    call print_char
    jmp .done

.chs_ok:
    mov al, 'C'
    call print_char
    mov al, ':'
    call print_char
    mov al, ch
    call print_hex_byte
    mov al, ','
    call print_char
    mov al, dh
    call print_hex_byte
    mov al, ','
    call print_char
    mov al, cl
    and al, 0x3F
    call print_hex_byte
    mov al, 0x0D
    call print_char
    mov al, 0x0A
    call print_char

.done:
    cli
    hlt

; ============================================================================
; Utility Functions
; ============================================================================

print_char:
    push ax
    mov ah, 0x0E
    int 0x10
    pop ax
    ret

print_hex_byte:
    push ax
    push bx
    mov bl, al
    shr al, 4
    and al, 0x0F
    cmp al, 0x09
    jle .high_digit
    add al, 'A' - 10
    jmp .print_high
.high_digit:
    add al, '0'
.print_high:
    call print_char

    mov al, bl
    and al, 0x0F
    cmp al, 0x09
    jle .low_digit
    add al, 'A' - 10
    jmp .print_low
.low_digit:
    add al, '0'
.print_low:
    call print_char

    pop bx
    pop ax
    ret

; ============================================================================
; Data
; ============================================================================

drive_num: db 0

times 510 - ($ - $$) db 0
dw 0xAA55
