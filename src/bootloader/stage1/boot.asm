bits 16

;
; FAT12 Bootloader (Stage 1)
;

section .fsjump
    jmp short start
    nop

section .fsheaders
bdb_oem:                    db "MSWIN4.1"           ; 8 bytes
bdb_bytes_per_sector:       dw 512
bdb_sectors_per_cluster:    db 1
bdb_reserved_sectors:       dw 33                   ; 1 boot sector + 32 reserved for stage 2
bdb_fat_count:              db 2
bdb_dir_entries_count:      dw 0E0h
bdb_total_sectors:          dw 2880                 ; 2880 * 512 = 1.44MB
bdb_media_descriptor_type:  db 0F0h                 ; 3.5" floppy / default
bdb_sectors_per_fat:        dw 9
bdb_sectors_per_track:      dw 18
bdb_heads:                  dw 2
bdb_hidden_sectors:         dd 0
bdb_large_sector_count:     dd 0

; Extended Boot Record
ebr_drive_number:           db 0
                            db 0                    ; reserved
ebr_signature:              db 29h
ebr_volume_id:              db 12h, 34h, 56h, 78h
ebr_volume_label:           db "MYOS       "        ; 11 bytes
ebr_system_id:              db "FAT12   "           ; 8 bytes

section .entry
global start
start:
    ; Preserve partition entry from MBR (passed in DS:SI) at 0x2000:0x0000
    mov ax, 0x2000
    mov es, ax
    mov di, 0x0000
    mov cx, 16
    rep movsb

    ; Initialize segment registers and stack
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Ensure CS:IP is 0000:7Cxx
    push es
    push word .after_cs
    retf

.after_cs:
    ; Save boot drive number passed in DL by BIOS
    mov [ebr_drive_number], dl

    ; Check for BIOS INT 13h LBA extensions
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [ebr_drive_number]
    stc
    int 13h
    jc .no_lba
    cmp bx, 0xAA55
    jne .no_lba

    ; LBA extensions supported
    mov byte [have_lba], 1
    jmp .load_stage2

.no_lba:
    mov byte [have_lba], 0

.load_stage2:
    mov si, msg_loading
    call puts

    ; Read stage 2 using stage2_location entries
    mov si, stage2_location
    mov ax, 0x0000
    mov es, ax
    mov bx, 0x0500

.s2_loop:
    mov eax, [si]
    add si, 4
    mov cl, [si]
    inc si

    cmp eax, 0
    je .s2_loaded

    call read_sectors
    jc .read_failed

    ; Advance ES by (cl * 512) / 16 = cl * 32
    xor ch, ch
    shl cx, 5
    mov di, es
    add di, cx
    mov es, di

    jmp .s2_loop

.s2_loaded:
    ; Far jump to Stage 2 with DL = boot drive, DI:SI = partition entry pointer (0x2000:0x0000)
    mov dl, [ebr_drive_number]
    mov di, 0x2000
    mov si, 0x0000
    jmp 0x0000:0x0500

.read_failed:
    mov si, msg_err
    call puts
.halt:
    cli
    hlt
    jmp .halt

section .text
;
; Reads sectors using LBA (AH=42h) if supported, or CHS (AH=02h) fallback
; Parameters:
;   EAX = starting LBA
;   CL  = number of sectors
;   ES:BX = target buffer
;
read_sectors:
    push eax
    push bx
    push cx
    push dx
    push si
    push di

    cmp byte [have_lba], 1
    jne .no_lba_read

    ; LBA mode read
    mov [dap.lba], eax
    mov [dap.offset], bx
    mov [dap.segment], es
    mov byte [dap.count], cl
    mov byte [dap.count+1], 0

    mov ah, 0x42
    mov dl, [ebr_drive_number]
    mov si, dap
    mov di, 3
.retry_lba:
    stc
    int 13h
    jnc .read_ok

    ; Reset disk
    push ax
    xor ax, ax
    mov dl, [ebr_drive_number]
    int 13h
    pop ax

    dec di
    jnz .retry_lba
    jmp .read_fail

.no_lba_read:
    ; CHS mode read (sector by sector)
    mov byte [sectors_left], cl
.chs_loop:
    mov di, 3
.retry_chs:
    push eax
    push bx

    call lba_to_chs
    mov dl, [ebr_drive_number]
    mov ax, 0x0201              ; read 1 sector
    stc
    int 13h
    pop bx
    pop eax
    jnc .chs_sector_ok

    ; Reset disk
    push ax
    xor ax, ax
    mov dl, [ebr_drive_number]
    int 13h
    pop ax

    dec di
    jnz .retry_chs
    jmp .read_fail

.chs_sector_ok:
    inc eax
    add bx, 512
    dec byte [sectors_left]
    jnz .chs_loop

.read_ok:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop eax
    clc
    ret

.read_fail:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop eax
    stc
    ret

;
; Converts LBA address in AX to CHS
; Returns:
;   CX [bits 0-5]: sector number (1-based)
;   CX [bits 6-15]: cylinder
;   DH: head
;
lba_to_chs:
    push ax
    push dx
    xor dx, dx
    div word [bdb_sectors_per_track]
    inc dx
    mov cx, dx

    xor dx, dx
    div word [bdb_heads]
    mov dh, dl
    mov ch, al
    shl ah, 6
    or cl, ah

    pop dx
    pop ax
    ret

;
; Print single character in AL to screen / serial
;
putc:
    out 0xe9, al
    push ax
    push bx
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    pop bx
    pop ax
    ret

;
; Print null-terminated string at DS:SI
;
puts:
    push si
    push ax
.loop:
    lodsb
    test al, al
    jz .done
    call putc
    jmp .loop
.done:
    pop ax
    pop si
    ret

section .data
have_lba:     db 0
sectors_left: db 0

; Disk Address Packet for INT 13h AH=42h
dap:
    .size:    db 16
    .rsvd:    db 0
    .count:   dw 0
    .offset:  dw 0
    .segment: dw 0
    .lba:     dq 0

msg_loading: db "Loading...", 13, 10, 0
msg_err:     db "Read error", 13, 10, 0

global stage2_location
stage2_location:
    dd 1            ; default start LBA = 1
    db 32           ; default sector count = 32
    times 25 db 0   ; extra location entries