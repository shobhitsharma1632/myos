#include "elf_loader.h"
#include <hal/vfs.h>
#include <hal/pmm.h>
#include <hal/vmm.h>
#include <memory.h>
#include <string.h>
#include <debug.h>

#define MODULE "ELFLoader"

#define PAGE_SIZE 4096

static inline uint32_t min_u32(uint32_t a, uint32_t b) { return (a < b) ? a : b; }
static inline uint32_t max_u32(uint32_t a, uint32_t b) { return (a > b) ? a : b; }

static uint32_t* ELF_CreatePageDirectory(void)
{
    uint32_t* kernel_pd = VMM_GetKernelPageDirectory();
    uint32_t* new_pd = (uint32_t*)PMM_AllocatePage();
    if (!new_pd)
    {
        log_err(MODULE, "Failed to allocate physical frame for new Page Directory!");
        return NULL;
    }
    memset(new_pd, 0, PAGE_SIZE);

    // Share kernel mappings:
    // Entries 0..255 (< 1 GiB): Identity map of physical RAM, kernel code/data, kernel heap
    // Entries 512..1022 (>= 2 GiB): Video framebuffers and memory-mapped I/O
    for (int i = 0; i < 1024; i++)
    {
        if (i == 1023)
        {
            // Recursive self-mapping points to the new Page Directory itself
            new_pd[1023] = ((uint32_t)new_pd & ~0xFFF) | PAGE_PRESENT | PAGE_WRITE;
        }
        else if (i < 256 || i >= 512)
        {
            if (kernel_pd)
                new_pd[i] = kernel_pd[i];
        }
    }

    return new_pd;
}

static bool ELF_MapPageInPD(uint32_t* pd, uint32_t vaddr, void* physAddr, ELF_ProgramImage* image)
{
    uint32_t pde_idx = (vaddr >> 22) & 0x3FF;
    uint32_t pte_idx = (vaddr >> 12) & 0x3FF;

    if (pde_idx == 1023)
        return false;

    // Check if Page Table exists
    if (!(pd[pde_idx] & PAGE_PRESENT))
    {
        if (image->pts_count >= ELF_MAX_TRACKED_PTS)
        {
            log_err(MODULE, "Exceeded maximum tracked page tables (%u)!", ELF_MAX_TRACKED_PTS);
            return false;
        }

        uint32_t* pt = (uint32_t*)PMM_AllocatePage();
        if (!pt)
        {
            log_err(MODULE, "Failed to allocate physical frame for user page table!");
            return false;
        }
        memset(pt, 0, PAGE_SIZE);

        pd[pde_idx] = ((uint32_t)pt & ~0xFFF) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        image->pts_phys[image->pts_count++] = pt;
    }

    // Get pointer to Page Table (accessible directly because physical RAM < 128 MB is identity-mapped)
    uint32_t* pt = (uint32_t*)(pd[pde_idx] & ~0xFFF);

    if (!(pt[pte_idx] & PAGE_PRESENT))
    {
        pt[pte_idx] = ((uint32_t)physAddr & ~0xFFF) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    return true;
}

bool ELF_Load(const char* filepath, ELF_ProgramImage* outImage)
{
    if (!filepath || !outImage)
        return false;

    memset(outImage, 0, sizeof(ELF_ProgramImage));

    fd_t fd = vfs_open(filepath, VFS_O_RDONLY);
    if (fd < 0)
    {
        // Try prepending "/bin/"
        char altPath[128];
        if (filepath[0] != '/')
        {
            strcpy(altPath, "/bin/");
            strcat(altPath, filepath);
            fd = vfs_open(altPath, VFS_O_RDONLY);
        }
    }

    if (fd < 0)
    {
        log_err(MODULE, "Could not open ELF executable: '%s'", filepath);
        return false;
    }

    // 1. Read ELF Header
    Elf32_Ehdr header;
    int n = vfs_read(fd, &header, sizeof(Elf32_Ehdr));
    if (n != sizeof(Elf32_Ehdr))
    {
        log_err(MODULE, "Failed to read ELF header from '%s' (got %d bytes)", filepath, n);
        vfs_close(fd);
        return false;
    }

    // 2. Validate ELF Header
    if (memcmp(header.magic, ELF_MAGIC, 4) != 0)
    {
        log_err(MODULE, "Invalid ELF magic in '%s'", filepath);
        vfs_close(fd);
        return false;
    }

    if (header.bitness != 1 || header.endianness != 1 || header.type != 2 || header.machine != 3)
    {
        log_err(MODULE, "Incompatible ELF binary '%s' (bitness=%u endian=%u type=%u machine=%u)",
                filepath, header.bitness, header.endianness, header.type, header.machine);
        vfs_close(fd);
        return false;
    }

    // 3. Create process Page Directory
    uint32_t* pd = ELF_CreatePageDirectory();
    if (!pd)
    {
        vfs_close(fd);
        return false;
    }
    outImage->page_directory_phys = (uint32_t)pd;

    // 4. Parse Program Headers
    for (uint16_t i = 0; i < header.phnum; i++)
    {
        Elf32_Phdr ph;
        vfs_lseek(fd, header.phoff + (i * header.phentsize), VFS_SEEK_SET);
        if (vfs_read(fd, &ph, sizeof(Elf32_Phdr)) != sizeof(Elf32_Phdr))
        {
            log_err(MODULE, "Failed to read program header %u from '%s'", i, filepath);
            ELF_Unload(outImage);
            vfs_close(fd);
            return false;
        }

        if (ph.type != PT_LOAD || ph.memsz == 0)
            continue;

        uint32_t seg_start = ph.vaddr;
        uint32_t seg_end   = ph.vaddr + ph.memsz;
        uint32_t page_start = PAGE_ALIGN_DOWN(seg_start);
        uint32_t page_end   = PAGE_ALIGN_UP(seg_end);

        for (uint32_t v = page_start; v < page_end; v += PAGE_SIZE)
        {
            // Check if page is already mapped in PD
            uint32_t pde_idx = (v >> 22) & 0x3FF;
            uint32_t pte_idx = (v >> 12) & 0x3FF;
            void* phys = NULL;

            if ((pd[pde_idx] & PAGE_PRESENT) && (((uint32_t*)(pd[pde_idx] & ~0xFFF))[pte_idx] & PAGE_PRESENT))
            {
                phys = (void*)(((uint32_t*)(pd[pde_idx] & ~0xFFF))[pte_idx] & ~0xFFF);
            }
            else
            {
                if (outImage->pages_count >= ELF_MAX_TRACKED_PAGES)
                {
                    log_err(MODULE, "Exceeded maximum tracked pages (%u)!", ELF_MAX_TRACKED_PAGES);
                    ELF_Unload(outImage);
                    vfs_close(fd);
                    return false;
                }

                phys = PMM_AllocatePage();
                if (!phys)
                {
                    log_err(MODULE, "Failed to allocate physical page for ELF segment!");
                    ELF_Unload(outImage);
                    vfs_close(fd);
                    return false;
                }
                memset(phys, 0, PAGE_SIZE);

                if (!ELF_MapPageInPD(pd, v, phys, outImage))
                {
                    PMM_FreePage(phys);
                    ELF_Unload(outImage);
                    vfs_close(fd);
                    return false;
                }

                outImage->pages_phys[outImage->pages_count] = phys;
                outImage->pages_virt[outImage->pages_count] = (void*)v;
                outImage->pages_count++;
            }

            // Copy file data if this page intersects with [ph.vaddr, ph.vaddr + ph.filesz)
            if (ph.filesz > 0)
            {
                uint32_t file_seg_start = ph.vaddr;
                uint32_t file_seg_end   = ph.vaddr + ph.filesz;
                uint32_t intersect_start = max_u32(v, file_seg_start);
                uint32_t intersect_end   = min_u32(v + PAGE_SIZE, file_seg_end);

                if (intersect_start < intersect_end)
                {
                    uint32_t chunk_bytes = intersect_end - intersect_start;
                    uint32_t page_offset = intersect_start - v;
                    uint32_t file_offset = ph.offset + (intersect_start - file_seg_start);

                    vfs_lseek(fd, file_offset, VFS_SEEK_SET);
                    vfs_read(fd, (uint8_t*)phys + page_offset, chunk_bytes);
                }
            }
        }
    }

    // 5. Map User Stack Pages
    uint32_t stack_base = USER_STACK_TOP - (USER_STACK_PAGES * PAGE_SIZE);
    for (uint32_t v = stack_base; v < USER_STACK_TOP; v += PAGE_SIZE)
    {
        if (outImage->pages_count >= ELF_MAX_TRACKED_PAGES)
        {
            log_err(MODULE, "Exceeded tracked pages while allocating user stack!");
            ELF_Unload(outImage);
            vfs_close(fd);
            return false;
        }

        void* phys_stack = PMM_AllocatePage();
        if (!phys_stack)
        {
            log_err(MODULE, "Failed to allocate physical frame for user stack!");
            ELF_Unload(outImage);
            vfs_close(fd);
            return false;
        }
        memset(phys_stack, 0, PAGE_SIZE);

        if (!ELF_MapPageInPD(pd, v, phys_stack, outImage))
        {
            PMM_FreePage(phys_stack);
            ELF_Unload(outImage);
            vfs_close(fd);
            return false;
        }

        outImage->pages_phys[outImage->pages_count] = phys_stack;
        outImage->pages_virt[outImage->pages_count] = (void*)v;
        outImage->pages_count++;
    }

    outImage->entry_point     = header.entry;
    outImage->user_stack_top  = USER_STACK_TOP;
    outImage->user_heap_break = 0x40200000;

    vfs_close(fd);
    log_info(MODULE, "Successfully loaded ELF '%s': entry=0x%x, PD=0x%x, pages=%u",
             filepath, outImage->entry_point, outImage->page_directory_phys, outImage->pages_count);
    return true;
}

void ELF_Unload(ELF_ProgramImage* image)
{
    if (!image) return;

    // Free all mapped program and stack physical pages
    for (size_t i = 0; i < image->pages_count; i++)
    {
        if (image->pages_phys[i])
        {
            PMM_FreePage(image->pages_phys[i]);
            image->pages_phys[i] = NULL;
        }
    }
    image->pages_count = 0;

    // Free allocated user Page Tables
    for (size_t i = 0; i < image->pts_count; i++)
    {
        if (image->pts_phys[i])
        {
            PMM_FreePage(image->pts_phys[i]);
            image->pts_phys[i] = NULL;
        }
    }
    image->pts_count = 0;

    // Free the Page Directory itself
    if (image->page_directory_phys)
    {
        PMM_FreePage((void*)image->page_directory_phys);
        image->page_directory_phys = 0;
    }
}
