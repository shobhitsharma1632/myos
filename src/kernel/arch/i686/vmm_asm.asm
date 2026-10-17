[bits 32]

global i686_VMM_LoadPageDirectory
i686_VMM_LoadPageDirectory:
    mov eax, [esp + 4]      ; Page directory physical address
    mov cr3, eax
    ret

global i686_VMM_GetPageDirectory
i686_VMM_GetPageDirectory:
    mov eax, cr3
    ret

global i686_VMM_EnablePaging
i686_VMM_EnablePaging:
    mov eax, cr0
    or eax, 0x80010000      ; Set PG (bit 31) and WP (bit 16)
    mov cr0, eax
    ret

global i686_VMM_DisablePaging
i686_VMM_DisablePaging:
    mov eax, cr0
    and eax, 0x7FFFFFFF     ; Clear PG (bit 31)
    mov cr0, eax
    ret

global i686_VMM_FlushTLB
i686_VMM_FlushTLB:
    mov eax, [esp + 4]      ; Virtual address to invalidate
    invlpg [eax]
    ret

global i686_VMM_GetFaultAddress
i686_VMM_GetFaultAddress:
    mov eax, cr2
    ret

global i686_VMM_IsPagingEnabled
i686_VMM_IsPagingEnabled:
    mov eax, cr0
    shr eax, 31             ; Extract bit 31 (PG)
    and eax, 1
    ret
