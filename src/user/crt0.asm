[bits 32]
section .entry

global _start
extern main
extern sys_exit

_start:
    ; Establish clean base pointer
    xor ebp, ebp

    ; The kernel pushed [dummy_ret_addr, argc, argv]
    ; [esp + 0] = dummy ret
    ; [esp + 4] = argc
    ; [esp + 8] = argv
    mov eax, [esp + 4]      ; argc
    mov edx, [esp + 8]      ; argv

    push edx                ; push argv
    push eax                ; push argc
    call main
    add esp, 8

    ; Terminate process with main's return code (in eax)
    push eax
    call sys_exit

.halt:
    jmp .halt
