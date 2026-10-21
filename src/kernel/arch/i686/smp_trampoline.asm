; smp_trampoline.asm - 16-bit to 32-bit AP startup trampoline
; Loaded at physical address 0x8000 by BSP

[bits 16]

global smp_trampoline_start
global smp_trampoline_end

smp_trampoline_start:
    cli
    cld

    ; Initialize 16-bit segment registers
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Load GDT from known offset 0x8040 (32-bit operand override ensures 6-byte descriptor)
    o32 lgdt [0x8040]

    ; Enable Protected Mode in CR0
    mov eax, cr0
    or al, 1
    mov cr0, eax

    ; Far jump to 32-bit protected mode code at 0x8060
    jmp dword 0x08:0x8060

times 0x40 - ($ - smp_trampoline_start) db 0

; Offset 0x40: GDT Descriptor (limit: 2 bytes, base: 4 bytes)
gdt_desc:
    dw 0
    dd 0
    dw 0 ; 2 bytes padding to align next field to 8 bytes

; Offset 0x48: CR3 (Page Directory physical base)
cr3_val:
    dd 0

; Offset 0x4C: ESP (Kernel stack top)
stack_top:
    dd 0

; Offset 0x50: C Entry Function Pointer
c_entry:
    dd 0

; Offset 0x54: Online Signal Flag
online_flag:
    dd 0

times 0x60 - ($ - smp_trampoline_start) db 0

[bits 32]
ap_pm_entry:
    ; Load 32-bit protected mode data segment selectors (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Load Kernel Page Directory into CR3
    mov eax, [0x8048]
    mov cr3, eax

    ; Enable Paging (PG bit 31, WP bit 16)
    mov eax, cr0
    or eax, 0x80010000
    mov cr0, eax

    ; Load dedicated AP kernel stack pointer
    mov esp, [0x804C]

    ; Signal online status to BSP
    mov dword [0x8054], 1

    ; Jump into C kernel entry function
    mov eax, [0x8050]
    call eax

.halt:
    hlt
    jmp .halt

smp_trampoline_end:
