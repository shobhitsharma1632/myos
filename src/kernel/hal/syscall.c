#include "syscall.h"
#include <arch/i686/idt.h>
#include <arch/i686/gdt.h>
#include <arch/i686/io.h>
#include <hal/task.h>
#include <hal/vmm.h>
#include <hal/pmm.h>
#include <hal/vfs.h>
#include <net/net.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <debug.h>

#define MODULE "Syscall"

extern void __attribute((cdecl)) i686_ISR128(void);

static bool g_SyscallYieldRequested = false;

void Syscall_RequestYield(void)
{
    g_SyscallYieldRequested = true;
}

bool Syscall_TakeYieldRequest(void)
{
    if (g_SyscallYieldRequested)
    {
        g_SyscallYieldRequested = false;
        return true;
    }
    return false;
}

void Syscall_Initialize(void)
{
    i686_ISR_RegisterHandler(0x80, Syscall_Handler);

    // Explicitly configure gate 0x80 (128) with IDT_FLAG_RING3 (DPL 3)
    // so software interrupts from Ring 3 succeed without #GP exception.
    i686_IDT_SetGate(0x80, i686_ISR128, i686_GDT_CODE_SEGMENT,
                     IDT_FLAG_PRESENT | IDT_FLAG_RING3 | IDT_FLAG_GATE_32BIT_INT);

    log_info(MODULE, "Syscall dispatcher initialized on int 0x80 with Ring 3 DPL.");
}

void Syscall_Handler(Registers* regs)
{
    uint32_t syscall_num = regs->eax;
    uint32_t arg1        = regs->ebx;
    uint32_t arg2        = regs->ecx;
    uint32_t arg3        = regs->edx;

    Task* current = Task_GetCurrent();

    switch (syscall_num)
    {
        case SYS_EXIT:
        {
            int32_t exit_code = (int32_t)arg1;
            log_info(MODULE, "[SYS_EXIT] Process '%s' (PID %u) terminated with exit code %d.",
                     current ? current->name : "unknown",
                     current ? current->id : 0,
                     exit_code);
            printf("[User Process] '%s' (PID %u) exited with code %d.\n",
                   current ? current->name : "unknown",
                   current ? current->id : 0,
                   exit_code);

            Task_ExitWithCode(exit_code);
            Syscall_RequestYield();
            regs->eax = 0;
            break;
        }

        case SYS_PRINT:
        {
            const char* str = (const char*)arg1;
            uint32_t len    = arg2;

            if (!str)
            {
                regs->eax = 0;
                break;
            }

            // Cap length to reasonable size (e.g. 4096 bytes)
            if (len > 4096) len = 4096;

            uint32_t written = 0;
            for (uint32_t i = 0; i < len; i++)
            {
                char c = str[i];
                if (c == '\0') break;
                putc(c);
                debugc(c);
                written++;
            }
            regs->eax = written;
            break;
        }

        case SYS_READ:
        {
            // Read from keyboard buffer or STDIN
            char* buf = (char*)arg1;
            uint32_t max_len = arg2;
            if (!buf || max_len == 0)
            {
                regs->eax = 0;
                break;
            }
            regs->eax = 0;
            break;
        }

        case SYS_YIELD:
        {
            Syscall_RequestYield();
            regs->eax = 0;
            break;
        }

        case SYS_GETPID:
        {
            regs->eax = current ? current->id : 0;
            break;
        }

        case SYS_SLEEP:
        {
            uint32_t ticks = arg1;
            if (current && ticks > 0)
            {
                current->state = TASK_SLEEPING;
                current->wake_tick = Task_GetTicks() + ticks;
                Syscall_RequestYield();
            }
            regs->eax = 0;
            break;
        }

        case SYS_GETTICKS:
        {
            regs->eax = Task_GetTicks();
            break;
        }

        case SYS_SBRK:
        {
            int32_t increment = (int32_t)arg1;
            if (!current || !current->is_user)
            {
                regs->eax = (uint32_t)-1;
                break;
            }

            uint32_t old_break = current->user_heap_break;
            if (increment == 0)
            {
                regs->eax = old_break;
                break;
            }

            uint32_t new_break = old_break + increment;
            if (new_break < 0x40200000 || new_break > 0x48000000)
            {
                regs->eax = (uint32_t)-1;
                break;
            }

            // Check if expanding into new 4 KiB pages
            uint32_t cur_page = (old_break + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint32_t req_page = (new_break + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

            while (cur_page < req_page)
            {
                if (current->elf_pages_count >= 256)
                {
                    regs->eax = (uint32_t)-1;
                    return;
                }

                void* p = PMM_AllocatePage();
                if (!p)
                {
                    regs->eax = (uint32_t)-1;
                    return;
                }
                memset(p, 0, PAGE_SIZE);

                // Check if page table exists for this PDE in current process's PD
                uint32_t pde_idx = (cur_page >> 22) & 0x3FF;
                if (!(VMM_PAGE_DIRECTORY_VIRT[pde_idx] & PAGE_PRESENT))
                {
                    void* new_pt = PMM_AllocatePage();
                    if (!new_pt)
                    {
                        PMM_FreePage(p);
                        regs->eax = (uint32_t)-1;
                        return;
                    }
                    memset(new_pt, 0, PAGE_SIZE);
                    if (current->elf_pts_count < 32)
                    {
                        current->elf_pts_phys[current->elf_pts_count++] = new_pt;
                    }
                    VMM_PAGE_DIRECTORY_VIRT[pde_idx] = ((uint32_t)new_pt & ~0xFFF) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
                    i686_VMM_FlushTLB((uint32_t)VMM_PAGE_TABLE_VIRT(pde_idx));
                }

                VMM_MapPage((void*)cur_page, p, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
                current->elf_pages_phys[current->elf_pages_count++] = p;
                cur_page += PAGE_SIZE;
            }

            current->user_heap_break = new_break;
            regs->eax = old_break;
            break;
        }

        case SYS_BEEP:
        {
            uint32_t freq = arg1;
            uint32_t duration_ms = arg2;
            log_info(MODULE, "[SYS_BEEP] freq=%u Hz, duration=%u ms", freq, duration_ms);
            regs->eax = 0;
            break;
        }

        case SYS_OPEN:
        {
            const char* path = (const char*)arg1;
            int flags = (int)arg2;
            if (!path)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)vfs_open(path, flags);
            break;
        }

        case SYS_CLOSE:
        {
            int fd = (int)arg1;
            regs->eax = (uint32_t)vfs_close(fd);
            break;
        }

        case SYS_READ_FILE:
        {
            int fd = (int)arg1;
            void* buf = (void*)arg2;
            size_t count = (size_t)arg3;
            if (!buf)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)vfs_read(fd, buf, count);
            break;
        }

        case SYS_WRITE_FILE:
        {
            int fd = (int)arg1;
            const void* buf = (const void*)arg2;
            size_t count = (size_t)arg3;
            if (!buf)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)vfs_write(fd, buf, count);
            break;
        }

        case SYS_READDIR:
        {
            const char* path = (const char*)arg1;
            size_t index = (size_t)arg2;
            VFS_DirEntry* entry = (VFS_DirEntry*)arg3;
            if (!entry)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)vfs_readdir(path, index, entry);
            break;
        }

        case SYS_STAT:
        {
            const char* path = (const char*)arg1;
            VFS_Stat* st = (VFS_Stat*)arg2;
            if (!st)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)vfs_stat(path, st);
            break;
        }

        case SYS_SPAWN:
        {
            const char* path = (const char*)arg1;
            int argc = (int)arg2;
            char* const* argv = (char* const*)arg3;

            if (!path)
            {
                regs->eax = (uint32_t)-1;
                break;
            }

            Task* child = Task_CreateUserFromELF(path, path, argc, argv);
            if (!child)
            {
                regs->eax = (uint32_t)-1;
            }
            else
            {
                regs->eax = child->id;
            }
            break;
        }

        case SYS_WAITPID:
        {
            int32_t pid = (int32_t)arg1;
            int32_t* status = (int32_t*)arg2;

            int32_t res = Task_WaitPID((uint32_t)pid, status);
            regs->eax = (uint32_t)res;
            break;
        }

        case SYS_SEEK:
        {
            int fd = (int)arg1;
            int32_t offset = (int32_t)arg2;
            int whence = (int)arg3;
            regs->eax = (uint32_t)vfs_lseek(fd, offset, whence);
            break;
        }

        case SYS_NET_INFO:
        {
            NetInfoUser* user_info = (NetInfoUser*)arg1;
            if (!user_info)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            NetInterface* netif = Net_GetInterface();
            if (!netif)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            memcpy(user_info->mac, netif->mac, 6);
            user_info->ip          = netif->ip;
            user_info->netmask     = netif->netmask;
            user_info->gateway     = netif->gateway;
            user_info->dns         = netif->dns;
            user_info->rx_packets  = netif->rx_packets;
            user_info->tx_packets  = netif->tx_packets;
            user_info->rx_bytes    = netif->rx_bytes;
            user_info->tx_bytes    = netif->tx_bytes;
            regs->eax = 0;
            break;
        }

        case SYS_NET_PING:
        {
            uint32_t target_ip = (uint32_t)arg1;
            uint32_t timeout_ms = (uint32_t)arg2;
            NetInterface* netif = Net_GetInterface();
            if (!netif)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            int32_t rtt = ICMP_Ping(netif, target_ip, timeout_ms);
            regs->eax = (uint32_t)rtt;
            break;
        }

        case SYS_NET_SEND_UDP:
        {
            uint32_t target_ip = (uint32_t)arg1;
            uint16_t src_port = (uint16_t)(arg2 >> 16);
            uint16_t dst_port = (uint16_t)(arg2 & 0xFFFF);
            const void* data = (const void*)arg3;
            size_t len = (size_t)regs->esi;
            NetInterface* netif = Net_GetInterface();
            if (!netif || !data)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            bool ok = UDP_Send(netif, target_ip, src_port, dst_port, data, len);
            regs->eax = ok ? 0 : (uint32_t)-1;
            break;
        }

        case SYS_PIPE:
        {
            int* pipefd = (int*)arg1;
            if (!pipefd)
            {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)vfs_pipe(pipefd);
            break;
        }

        case SYS_DUP:
        {
            int oldfd = (int)arg1;
            regs->eax = (uint32_t)vfs_dup(oldfd);
            break;
        }

        case SYS_DUP2:
        {
            int oldfd = (int)arg1;
            int newfd = (int)arg2;
            regs->eax = (uint32_t)vfs_dup2(oldfd, newfd);
            break;
        }

        case SYS_SOCKET:
        {
            int domain = (int)arg1;
            int type = (int)arg2;
            int protocol = (int)arg3;
            (void)protocol;

            if (domain != AF_INET || type != SOCK_STREAM)
            {
                regs->eax = (uint32_t)-1;
                break;
            }

            TCPSocket* sock = TCP_SocketCreate();
            if (!sock)
            {
                regs->eax = (uint32_t)-1;
                break;
            }

            int fd = vfs_socket_create(sock);
            regs->eax = (uint32_t)fd;
            break;
        }

        case SYS_BIND:
        {
            int fd = (int)arg1;
            const struct sockaddr* addr = (const struct sockaddr*)arg2;
            socklen_t len = (socklen_t)arg3;

            TCPSocket* sock = (TCPSocket*)vfs_socket_get(fd);
            if (!sock || !addr || len < sizeof(struct sockaddr_in))
            {
                regs->eax = (uint32_t)-1;
                break;
            }

            const struct sockaddr_in* in = (const struct sockaddr_in*)addr;
            regs->eax = (uint32_t)TCP_Bind(sock, in->sin_addr.s_addr, ntohs(in->sin_port));
            break;
        }

        case SYS_LISTEN:
        {
            int fd = (int)arg1;
            int backlog = (int)arg2;

            TCPSocket* sock = (TCPSocket*)vfs_socket_get(fd);
            if (!sock)
            {
                regs->eax = (uint32_t)-1;
                break;
            }

            regs->eax = (uint32_t)TCP_Listen(sock, backlog);
            break;
        }

        case SYS_CONNECT:
        {
            int fd = (int)arg1;
            const struct sockaddr* addr = (const struct sockaddr*)arg2;
            socklen_t len = (socklen_t)arg3;

            TCPSocket* sock = (TCPSocket*)vfs_socket_get(fd);
            if (!sock || !addr || len < sizeof(struct sockaddr_in))
            {
                regs->eax = (uint32_t)-1;
                break;
            }

            const struct sockaddr_in* in = (const struct sockaddr_in*)addr;
            regs->eax = (uint32_t)TCP_Connect(sock, in->sin_addr.s_addr, ntohs(in->sin_port));
            break;
        }

        case SYS_ACCEPT:
        {
            int fd = (int)arg1;
            struct sockaddr* addr = (struct sockaddr*)arg2;
            socklen_t* len = (socklen_t*)arg3;

            TCPSocket* sock = (TCPSocket*)vfs_socket_get(fd);
            if (!sock)
            {
                regs->eax = (uint32_t)-1;
                break;
            }

            uint32_t client_ip = 0;
            uint16_t client_port = 0;
            TCPSocket* client = TCP_Accept(sock, &client_ip, &client_port);
            if (!client)
            {
                regs->eax = (uint32_t)-1;
                break;
            }

            if (addr && len && *len >= sizeof(struct sockaddr_in))
            {
                struct sockaddr_in* in = (struct sockaddr_in*)addr;
                in->sin_family = AF_INET;
                in->sin_port = htons(client_port);
                in->sin_addr.s_addr = client_ip;
                *len = sizeof(struct sockaddr_in);
            }

            int client_fd = vfs_socket_create(client);
            regs->eax = (uint32_t)client_fd;
            break;
        }

        case SYS_SEND:
        {
            int fd = (int)arg1;
            const void* buf = (const void*)arg2;
            size_t count = (size_t)arg3;

            TCPSocket* sock = (TCPSocket*)vfs_socket_get(fd);
            if (!sock || !buf)
            {
                regs->eax = (uint32_t)-1;
                break;
            }

            regs->eax = (uint32_t)TCP_Send(sock, buf, count);
            break;
        }

        case SYS_RECV:
        {
            int fd = (int)arg1;
            void* buf = (void*)arg2;
            size_t count = (size_t)arg3;

            TCPSocket* sock = (TCPSocket*)vfs_socket_get(fd);
            if (!sock || !buf)
            {
                regs->eax = (uint32_t)-1;
                break;
            }

            regs->eax = (uint32_t)TCP_Recv(sock, buf, count);
            break;
        }

        default:
        {
            log_warn(MODULE, "Unknown syscall %u called by '%s' (PID %u)",
                     syscall_num, current ? current->name : "unknown", current ? current->id : 0);
            regs->eax = (uint32_t)-1;
            break;
        }
    }
}
