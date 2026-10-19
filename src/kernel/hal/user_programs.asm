[bits 32]

global user_program_hello
global user_program_hello_end
global user_program_counter
global user_program_counter_end
global user_program_fault_gp
global user_program_fault_gp_end
global user_program_fault_pf
global user_program_fault_pf_end

section .text

; --------------------------------------------------------------------------
; Program 1: user_program_hello
; Tests Ring 3 user mode execution, int 0x80 SYS_PRINT, SYS_GETPID, SYS_YIELD, SYS_EXIT
; --------------------------------------------------------------------------
user_program_hello:
    ; Get current instruction pointer for position-independent string addressing
    call .get_ip
.get_ip:
    pop ebx
    add ebx, (.msg - .get_ip)   ; ebx = pointer to .msg
    mov eax, 1                  ; SYS_PRINT
    mov ecx, .msg_len           ; ecx = length
    int 0x80

    ; SYS_GETPID (4)
    mov eax, 4
    int 0x80

    ; SYS_YIELD (3)
    mov eax, 3
    int 0x80

    ; SYS_EXIT (0) with code 0
    mov eax, 0
    xor ebx, ebx
    int 0x80

.hang:
    jmp .hang

.msg: db "[Ring 3 User Mode] Hello from protected user space! Syscall int 0x80 works!", 10, 0
.msg_len equ $ - .msg
user_program_hello_end:

; --------------------------------------------------------------------------
; Program 2: user_program_counter
; Tests iterative execution, stack usage in Ring 3, and non-blocking SYS_SLEEP
; --------------------------------------------------------------------------
user_program_counter:
    call .get_ip_c
.get_ip_c:
    pop ebx
    add ebx, (.start_msg - .get_ip_c)
    mov eax, 1                  ; SYS_PRINT
    mov ecx, .start_len
    int 0x80

    ; Loop 3 times with SYS_SLEEP 10 ticks (~100ms)
    mov esi, 3
.loop:
    push esi

    ; SYS_SLEEP (5), ebx = 10 ticks
    mov eax, 5
    mov ebx, 10
    int 0x80

    pop esi
    dec esi
    jnz .loop

    call .get_ip_c2
.get_ip_c2:
    pop ebx
    add ebx, (.end_msg - .get_ip_c2)
    mov eax, 1                  ; SYS_PRINT
    mov ecx, .end_len
    int 0x80

    ; SYS_EXIT (0) with code 42
    mov eax, 0
    mov ebx, 42
    int 0x80

.hang_c:
    jmp .hang_c

.start_msg: db "[User Counter] Ring 3 counter task started. Testing sleep syscalls...", 10, 0
.start_len equ $ - .start_msg
.end_msg: db "[User Counter] Sleep cycles completed successfully! Exiting with code 42.", 10, 0
.end_len equ $ - .end_msg
user_program_counter_end:

; --------------------------------------------------------------------------
; Program 3: user_program_fault_gp
; Verifies hardware fault isolation: attempting privileged 'cli' in Ring 3
; triggers #GP (interrupt 13). Kernel must terminate this task without crashing.
; --------------------------------------------------------------------------
user_program_fault_gp:
    call .get_ip_gp
.get_ip_gp:
    pop ebx
    add ebx, (.gp_msg - .get_ip_gp)
    mov eax, 1                  ; SYS_PRINT
    mov ecx, .gp_len
    int 0x80

    ; Privileged instruction in Ring 3! (IOPL = 0, CPL = 3)
    cli

    ; Should never reach here
    mov eax, 0
    int 0x80

.gp_msg: db "[Fault Test #GP] Executing privileged 'cli' in Ring 3 (expect #GP)...", 10, 0
.gp_len equ $ - .gp_msg
user_program_fault_gp_end:

; --------------------------------------------------------------------------
; Program 4: user_program_fault_pf
; Verifies hardware MMU memory isolation: attempting to read supervisor memory
; (0x00100000 - 1MB kernel load address) in Ring 3 triggers #PF (interrupt 14).
; Kernel must terminate this task without crashing.
; --------------------------------------------------------------------------
user_program_fault_pf:
    call .get_ip_pf
.get_ip_pf:
    pop ebx
    add ebx, (.pf_msg - .get_ip_pf)
    mov eax, 1                  ; SYS_PRINT
    mov ecx, .pf_len
    int 0x80

    ; Read supervisor kernel memory at 1MB! (CR0.WP=1, PAGE_USER=0)
    mov eax, [0x00100000]

    ; Should never reach here
    mov eax, 0
    int 0x80

.pf_msg: db "[Fault Test #PF] Accessing protected kernel address 0x100000 (expect #PF)...", 10, 0
.pf_len equ $ - .pf_msg
user_program_fault_pf_end:
