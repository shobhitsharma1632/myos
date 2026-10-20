#include "e1000.h"
#include <arch/i686/pci.h>
#include <arch/i686/isr.h>
#include <arch/i686/irq.h>
#include <arch/i686/io.h>
#include <hal/vmm.h>
#include <hal/pmm.h>
#include <memory.h>
#include <string.h>
#include <debug.h>

#define MODULE "E1000"

// E1000 Register Offsets
#define E1000_REG_CTRL         0x0000
#define E1000_REG_STATUS       0x0008
#define E1000_REG_EECD         0x0010
#define E1000_REG_EERD         0x0014
#define E1000_REG_ICR          0x00C0
#define E1000_REG_IMS          0x00D0
#define E1000_REG_IMC          0x00D8
#define E1000_REG_RCTL         0x0100
#define E1000_REG_TCTL         0x0400
#define E1000_REG_TIPG         0x0410
#define E1000_REG_RDBAL        0x2800
#define E1000_REG_RDBAH        0x2804
#define E1000_REG_RDLEN        0x2808
#define E1000_REG_RDH          0x2810
#define E1000_REG_RDT          0x2818
#define E1000_REG_TDBAL        0x3800
#define E1000_REG_TDBAH        0x3804
#define E1000_REG_TDLEN        0x3808
#define E1000_REG_TDH          0x3810
#define E1000_REG_TDT          0x3818
#define E1000_REG_MTA          0x5200
#define E1000_REG_RAL0         0x5400
#define E1000_REG_RAH0         0x5404

// Control Register Bits
#define E1000_CTRL_FD          (1 << 0)
#define E1000_CTRL_LRST        (1 << 3)
#define E1000_CTRL_ASDE        (1 << 5)
#define E1000_CTRL_SLU         (1 << 6)
#define E1000_CTRL_RST         (1 << 26)

// Status Register Bits
#define E1000_STATUS_FD        (1 << 0)
#define E1000_STATUS_LU        (1 << 1)

// Receive Control Bits
#define E1000_RCTL_EN          (1 << 1)
#define E1000_RCTL_SBP         (1 << 2)
#define E1000_RCTL_UPE         (1 << 3)
#define E1000_RCTL_MPE         (1 << 4)
#define E1000_RCTL_LPE         (1 << 5)
#define E1000_RCTL_BAM         (1 << 15)
#define E1000_RCTL_BSIZE_2048  (0 << 16)
#define E1000_RCTL_SECRC       (1 << 26)

// Transmit Control Bits
#define E1000_TCTL_EN          (1 << 1)
#define E1000_TCTL_PSP         (1 << 3)
#define E1000_TCTL_CT_SHIFT    4
#define E1000_TCTL_COLD_SHIFT  12

#define E1000_NUM_RX_DESC      32
#define E1000_NUM_TX_DESC      16
#define E1000_RX_BUFFER_SIZE   2048
#define E1000_TX_BUFFER_SIZE   2048

#define E1000_MMIO_VIRT_BASE   0xFE000000

typedef struct {
    volatile uint64_t buffer_addr;
    volatile uint16_t length;
    volatile uint16_t checksum;
    volatile uint8_t  status;
    volatile uint8_t  errors;
    volatile uint16_t special;
} __attribute__((packed)) E1000_RxDesc;

typedef struct {
    volatile uint64_t buffer_addr;
    volatile uint16_t length;
    volatile uint8_t  cso;
    volatile uint8_t  cmd;
    volatile uint8_t  status;
    volatile uint8_t  css;
    volatile uint16_t special;
} __attribute__((packed)) E1000_TxDesc;

static E1000Device   g_Dev;
static E1000_RxDesc* g_RxDesc = NULL;
static E1000_TxDesc* g_TxDesc = NULL;
static uint8_t*      g_RxBuffers[E1000_NUM_RX_DESC];
static uint8_t*      g_TxBuffers[E1000_NUM_TX_DESC];
static uint16_t      g_RxCur = 0;
static uint16_t      g_TxCur = 0;

// Forward declaration of network layer packet consumer
extern void Net_ReceivePacket(const void* packet, size_t len);

static inline uint32_t e1000_read_reg(uint32_t offset)
{
    return *(volatile uint32_t*)(g_Dev.mem_base + offset);
}

static inline void e1000_write_reg(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t*)(g_Dev.mem_base + offset) = val;
}

static uint16_t e1000_read_eeprom(uint8_t addr)
{
    e1000_write_reg(E1000_REG_EERD, 1 | ((uint32_t)addr << 8));
    uint32_t val = 0;
    uint32_t timeout = 100000;
    while (!((val = e1000_read_reg(E1000_REG_EERD)) & (1 << 4)) && --timeout)
    {
        __asm__ volatile("pause");
    }
    return (uint16_t)((val >> 16) & 0xFFFF);
}

static void e1000_read_mac(void)
{
    // First try reading RAL0 / RAH0
    uint32_t low = e1000_read_reg(E1000_REG_RAL0);
    uint32_t high = e1000_read_reg(E1000_REG_RAH0);

    if (high & (1 << 31))
    {
        g_Dev.mac[0] = low & 0xFF;
        g_Dev.mac[1] = (low >> 8) & 0xFF;
        g_Dev.mac[2] = (low >> 16) & 0xFF;
        g_Dev.mac[3] = (low >> 24) & 0xFF;
        g_Dev.mac[4] = high & 0xFF;
        g_Dev.mac[5] = (high >> 8) & 0xFF;
    }
    else
    {
        // Fallback to EEPROM reading
        uint16_t val0 = e1000_read_eeprom(0);
        uint16_t val1 = e1000_read_eeprom(1);
        uint16_t val2 = e1000_read_eeprom(2);

        g_Dev.mac[0] = val0 & 0xFF;
        g_Dev.mac[1] = (val0 >> 8) & 0xFF;
        g_Dev.mac[2] = val1 & 0xFF;
        g_Dev.mac[3] = (val1 >> 8) & 0xFF;
        g_Dev.mac[4] = val2 & 0xFF;
        g_Dev.mac[5] = (val2 >> 8) & 0xFF;
    }
}

static void e1000_irq_handler(Registers* regs)
{
    (void)regs;
    // Reading ICR automatically acknowledges and clears pending interrupt flags
    uint32_t status = e1000_read_reg(E1000_REG_ICR);
    if (status & 0x04)
    {
        // Link status change
        uint32_t ctrl = e1000_read_reg(E1000_REG_CTRL);
        e1000_write_reg(E1000_REG_CTRL, ctrl | E1000_CTRL_SLU);
    }

    // Process any incoming packets
    E1000_Poll();
}

bool E1000_Initialize(void)
{
    memset(&g_Dev, 0, sizeof(g_Dev));

    // Scan PCI for supported Intel Gigabit Ethernet controllers
    uint16_t dev_ids[] = {
        E1000_DEV_82540EM,
        E1000_DEV_82545EM,
        E1000_DEV_82543GC,
        E1000_DEV_82541PI
    };

    bool found = false;
    for (size_t i = 0; i < sizeof(dev_ids) / sizeof(dev_ids[0]); i++)
    {
        if (pci_find_device(E1000_VENDOR_INTEL, dev_ids[i], &g_Dev.bus, &g_Dev.slot, &g_Dev.func))
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        log_info(MODULE, "No Intel E1000 NIC found on PCI bus.");
        return false;
    }

    // Enable PCI Bus Mastering and Memory Space
    uint16_t cmd = pci_read_word(g_Dev.bus, g_Dev.slot, g_Dev.func, PCI_COMMAND);
    pci_write_dword(g_Dev.bus, g_Dev.slot, g_Dev.func, PCI_COMMAND, cmd | 0x07);

    // Read BAR0 (MMIO Base)
    g_Dev.mem_phys = pci_get_bar(g_Dev.bus, g_Dev.slot, g_Dev.func, 0);
    g_Dev.irq = pci_read_byte(g_Dev.bus, g_Dev.slot, g_Dev.func, 0x3C);
    g_Dev.mem_base = E1000_MMIO_VIRT_BASE;

    log_info(MODULE, "Found Intel E1000 at PCI %02x:%02x.%x | BAR0: 0x%x | IRQ: %u",
             g_Dev.bus, g_Dev.slot, g_Dev.func, g_Dev.mem_phys, g_Dev.irq);

    // Map 128 KiB (32 pages) of MMIO space into virtual memory
    for (uint32_t p = 0; p < 32; p++)
    {
        VMM_MapPage((void*)(g_Dev.mem_base + (p * 4096)),
                    (void*)(g_Dev.mem_phys + (p * 4096)),
                    PAGE_PRESENT | PAGE_WRITE);
    }

    // 1. Device Reset
    e1000_write_reg(E1000_REG_CTRL, e1000_read_reg(E1000_REG_CTRL) | E1000_CTRL_RST);
    for (volatile int i = 0; i < 50000; i++) { __asm__ volatile("nop"); }

    // Re-enable Bus Mastering in case reset cleared it
    pci_write_dword(g_Dev.bus, g_Dev.slot, g_Dev.func, PCI_COMMAND, cmd | 0x07);

    // 2. Set Link Up and Auto-Speed Detection
    e1000_write_reg(E1000_REG_CTRL, E1000_CTRL_SLU | E1000_CTRL_ASDE | E1000_CTRL_FD);

    // 3. Clear Multicast Table Array (MTA)
    for (int i = 0; i < 128; i++)
    {
        e1000_write_reg(E1000_REG_MTA + (i * 4), 0);
    }

    // 4. Read MAC address & program RAL0/RAH0 with Address Valid bit
    e1000_read_mac();
    uint32_t ral = (uint32_t)g_Dev.mac[0] |
                   ((uint32_t)g_Dev.mac[1] << 8) |
                   ((uint32_t)g_Dev.mac[2] << 16) |
                   ((uint32_t)g_Dev.mac[3] << 24);
    uint32_t rah = (uint32_t)g_Dev.mac[4] |
                   ((uint32_t)g_Dev.mac[5] << 8) |
                   (1U << 31); // E1000_RAH_AV

    e1000_write_reg(E1000_REG_RAL0, ral);
    e1000_write_reg(E1000_REG_RAH0, rah);

    log_info(MODULE, "MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",
             g_Dev.mac[0], g_Dev.mac[1], g_Dev.mac[2],
             g_Dev.mac[3], g_Dev.mac[4], g_Dev.mac[5]);

    // 5. Allocate Descriptors and Buffers
    // Allocate 1 page (4 KiB) for descriptors:
    // Offset 0: Rx Descriptors (32 * 16 = 512 bytes)
    // Offset 512: Tx Descriptors (16 * 16 = 256 bytes)
    void* desc_page = PMM_AllocatePage();
    if (!desc_page)
    {
        log_err(MODULE, "Failed to allocate memory for E1000 descriptors!");
        return false;
    }
    memset(desc_page, 0, 4096);

    g_RxDesc = (E1000_RxDesc*)desc_page;
    g_TxDesc = (E1000_TxDesc*)((uint8_t*)desc_page + 512);

    // Allocate Rx Packet Buffers: 32 buffers of 2048 bytes = 16 pages
    for (int i = 0; i < E1000_NUM_RX_DESC; i += 2)
    {
        void* rx_page = PMM_AllocatePage();
        if (!rx_page)
        {
            log_err(MODULE, "Failed to allocate physical page for Rx buffers!");
            return false;
        }
        memset(rx_page, 0, 4096);
        g_RxBuffers[i]     = (uint8_t*)rx_page;
        g_RxBuffers[i + 1] = (uint8_t*)rx_page + 2048;

        g_RxDesc[i].buffer_addr     = (uint32_t)g_RxBuffers[i];
        g_RxDesc[i].status          = 0;
        g_RxDesc[i + 1].buffer_addr = (uint32_t)g_RxBuffers[i + 1];
        g_RxDesc[i + 1].status      = 0;
    }

    // Allocate Tx Packet Buffers: 16 buffers of 2048 bytes = 8 pages
    for (int i = 0; i < E1000_NUM_TX_DESC; i += 2)
    {
        void* tx_page = PMM_AllocatePage();
        if (!tx_page)
        {
            log_err(MODULE, "Failed to allocate physical page for Tx buffers!");
            return false;
        }
        memset(tx_page, 0, 4096);
        g_TxBuffers[i]     = (uint8_t*)tx_page;
        g_TxBuffers[i + 1] = (uint8_t*)tx_page + 2048;

        g_TxDesc[i].buffer_addr     = (uint32_t)g_TxBuffers[i];
        g_TxDesc[i].status          = 0;
        g_TxDesc[i + 1].buffer_addr = (uint32_t)g_TxBuffers[i + 1];
        g_TxDesc[i + 1].status      = 0;
    }

    // 6. Initialize Receive Ring
    e1000_write_reg(E1000_REG_RDBAL, (uint32_t)g_RxDesc);
    e1000_write_reg(E1000_REG_RDBAH, 0);
    e1000_write_reg(E1000_REG_RDLEN, E1000_NUM_RX_DESC * sizeof(E1000_RxDesc));
    e1000_write_reg(E1000_REG_RDH, 0);
    e1000_write_reg(E1000_REG_RDT, E1000_NUM_RX_DESC - 1);
    g_RxCur = 0;

    // Enable Receiver with Broadcast Accept Mode and 2048 buffer size
    e1000_write_reg(E1000_REG_RCTL,
                    E1000_RCTL_EN |
                    E1000_RCTL_SBP |
                    E1000_RCTL_UPE |
                    E1000_RCTL_MPE |
                    E1000_RCTL_BAM |
                    E1000_RCTL_BSIZE_2048 |
                    E1000_RCTL_SECRC);

    // 7. Initialize Transmit Ring
    e1000_write_reg(E1000_REG_TDBAL, (uint32_t)g_TxDesc);
    e1000_write_reg(E1000_REG_TDBAH, 0);
    e1000_write_reg(E1000_REG_TDLEN, E1000_NUM_TX_DESC * sizeof(E1000_TxDesc));
    e1000_write_reg(E1000_REG_TDH, 0);
    e1000_write_reg(E1000_REG_TDT, 0);
    g_TxCur = 0;

    // Inter-Packet Gap (IPG) standard IEEE 802.3 values
    e1000_write_reg(E1000_REG_TIPG, (10 & 0x3FF) | ((8 & 0x3FF) << 10) | ((6 & 0x3FF) << 20));

    // Enable Transmitter with Padding and Collision parameters
    e1000_write_reg(E1000_REG_TCTL,
                    E1000_TCTL_EN |
                    E1000_TCTL_PSP |
                    (0x0F << E1000_TCTL_CT_SHIFT) |
                    (0x40 << E1000_TCTL_COLD_SHIFT));

    // 8. Register IRQ & Enable Interrupts
    if (g_Dev.irq < 16)
    {
        i686_IRQ_RegisterHandler(g_Dev.irq, e1000_irq_handler);
    }

    // Enable Receiver Timer and Packet Threshold Interrupts
    e1000_write_reg(E1000_REG_IMS, 0x1F6DC);
    e1000_read_reg(E1000_REG_ICR); // Read to clear any stale interrupt

    // Check Link Status
    uint32_t status = e1000_read_reg(E1000_REG_STATUS);
    bool linkUp = (status & E1000_STATUS_LU) != 0;
    log_info(MODULE, "Intel E1000 initialized successfully! Link: %s (%s duplex)",
             linkUp ? "UP" : "DOWN",
             (status & E1000_STATUS_FD) ? "Full" : "Half");

    g_Dev.present = true;
    return true;
}

bool E1000_IsPresent(void)
{
    return g_Dev.present;
}

const uint8_t* E1000_GetMAC(void)
{
    return g_Dev.mac;
}

bool E1000_SendPacket(const void* data, size_t len)
{
    if (!g_Dev.present || !data || len == 0 || len > E1000_TX_BUFFER_SIZE)
        return false;

    uint8_t* tx_buf = g_TxBuffers[g_TxCur];
    memcpy(tx_buf, data, len);

    g_TxDesc[g_TxCur].buffer_addr = (uint32_t)tx_buf;
    g_TxDesc[g_TxCur].length      = (uint16_t)len;
    // Command flags: bit 0 = EOP (End of Packet), bit 1 = IFCS (Insert FCS), bit 3 = RS (Report Status)
    g_TxDesc[g_TxCur].cmd         = 0x0B;
    g_TxDesc[g_TxCur].status      = 0;

    uint16_t old_cur = g_TxCur;
    g_TxCur = (g_TxCur + 1) % E1000_NUM_TX_DESC;

    e1000_write_reg(E1000_REG_TDT, g_TxCur);

    // Wait briefly for completion
    uint32_t timeout = 50000;
    while (!(g_TxDesc[old_cur].status & 0x01) && --timeout)
    {
        __asm__ volatile("pause");
    }

    g_Dev.tx_packets++;
    g_Dev.tx_bytes += len;
    return true;
}

void E1000_Poll(void)
{
    if (!g_Dev.present || !g_RxDesc)
        return;

    while (g_RxDesc[g_RxCur].status & 0x01) // Descriptor Done (DD)
    {
        uint16_t len = g_RxDesc[g_RxCur].length;
        uint8_t* buf = g_RxBuffers[g_RxCur];

        if (len > 0)
        {
            g_Dev.rx_packets++;
            g_Dev.rx_bytes += len;
            Net_ReceivePacket(buf, len);
        }

        g_RxDesc[g_RxCur].status = 0;
        uint16_t old_cur = g_RxCur;
        g_RxCur = (g_RxCur + 1) % E1000_NUM_RX_DESC;
        e1000_write_reg(E1000_REG_RDT, old_cur);
    }
}

void E1000_GetStats(uint32_t* rx_pkts, uint32_t* tx_pkts, uint32_t* rx_bytes, uint32_t* tx_bytes)
{
    if (rx_pkts)  *rx_pkts  = g_Dev.rx_packets;
    if (tx_pkts)  *tx_pkts  = g_Dev.tx_packets;
    if (rx_bytes) *rx_bytes = g_Dev.rx_bytes;
    if (tx_bytes) *tx_bytes = g_Dev.tx_bytes;
}
