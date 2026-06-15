/* Intel e1000 NIC driver implementation. */
#include "e1000.h"
#include "ethernet.h"
#include "netbuf.h"
#include "pci.h"
#include "pci_device.h"
#include "vmm.h"
#include "pmm.h"
#include "memory_layout.h"
#include "string.h"
#include "log.h"
#include "fmt.h"

static struct e1000_device g_e1000;

/* ---- MMIO register access ------------------------------------------------- */
static inline u32 e1000_read(struct e1000_device *dev, u32 reg) {
    return *(volatile u32 *)(dev->mmio + reg);
}
static inline void e1000_write(struct e1000_device *dev, u32 reg, u32 val) {
    *(volatile u32 *)(dev->mmio + reg) = val;
}

/* ---- EEPROM ---------------------------------------------------------------
 * QEMU's e1000 supports the EERD register: write the address with the START
 * bit, poll for DONE, read the data out of the high 16 bits. */
static int e1000_detect_eeprom(struct e1000_device *dev) {
    e1000_write(dev, E1000_REG_EERD, 0x1u);   /* start read of word 0 */
    for (int i = 0; i < 1000; i++) {
        u32 v = e1000_read(dev, E1000_REG_EERD);
        if (v & (1u << 4)) {   /* DONE bit (82540 uses bit 4) */
            return 1;
        }
    }
    return 0;
}

static u16 e1000_eeprom_read(struct e1000_device *dev, u8 word) {
    e1000_write(dev, E1000_REG_EERD, ((u32)word << 8) | 0x1u);
    for (int i = 0; i < 100000; i++) {
        u32 v = e1000_read(dev, E1000_REG_EERD);
        if (v & (1u << 4)) {
            return (u16)(v >> 16);
        }
    }
    return 0;
}

static void e1000_read_mac(struct e1000_device *dev) {
    if (dev->has_eeprom) {
        for (int i = 0; i < 3; i++) {
            u16 w = e1000_eeprom_read(dev, (u8)i);
            dev->mac.addr[i * 2]     = (u8)(w & 0xFF);
            dev->mac.addr[i * 2 + 1] = (u8)(w >> 8);
        }
        return;
    }
    /* No EEPROM: read from the Receive Address registers (RAL/RAH[0]). */
    u32 ral = e1000_read(dev, E1000_REG_RAL0);
    u32 rah = e1000_read(dev, E1000_REG_RAH0);
    dev->mac.addr[0] = (u8)(ral & 0xFF);
    dev->mac.addr[1] = (u8)(ral >> 8);
    dev->mac.addr[2] = (u8)(ral >> 16);
    dev->mac.addr[3] = (u8)(ral >> 24);
    dev->mac.addr[4] = (u8)(rah & 0xFF);
    dev->mac.addr[5] = (u8)(rah >> 8);
}

/* Program our MAC into RAL/RAH[0] with the Address Valid bit. */
static void e1000_set_mac(struct e1000_device *dev) {
    const u8 *m = dev->mac.addr;
    u32 ral = (u32)m[0] | ((u32)m[1] << 8) | ((u32)m[2] << 16) |
              ((u32)m[3] << 24);
    u32 rah = (u32)m[4] | ((u32)m[5] << 8) | (1u << 31);   /* AV */
    e1000_write(dev, E1000_REG_RAL0, ral);
    e1000_write(dev, E1000_REG_RAH0, rah);
}

/* ---- Ring setup ----------------------------------------------------------- */
static int e1000_setup_rx(struct e1000_device *dev) {
    u64 ring_phys = pmm_alloc_page();
    if (ring_phys == 0) {
        return -1;
    }
    dev->rx_ring = (struct e1000_rx_desc *)(uintptr_t)ring_phys;
    memset(dev->rx_ring, 0, PAGE_SIZE);

    for (unsigned i = 0; i < E1000_NUM_RX_DESC; i++) {
        u64 buf = pmm_alloc_page();   /* 4 KiB holds the 2 KiB RX buffer */
        if (buf == 0) {
            return -1;
        }
        dev->rx_buffers[i] = (u8 *)(uintptr_t)buf;
        dev->rx_ring[i].addr = buf;
        dev->rx_ring[i].status = 0;
    }

    e1000_write(dev, E1000_REG_RDBAL, (u32)(ring_phys & 0xFFFFFFFFu));
    e1000_write(dev, E1000_REG_RDBAH, (u32)(ring_phys >> 32));
    e1000_write(dev, E1000_REG_RDLEN,
                E1000_NUM_RX_DESC * (u32)sizeof(struct e1000_rx_desc));
    e1000_write(dev, E1000_REG_RDH, 0);
    e1000_write(dev, E1000_REG_RDT, E1000_NUM_RX_DESC - 1);
    dev->rx_cur = 0;

    e1000_write(dev, E1000_REG_RCTL,
                E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC |
                E1000_RCTL_BSIZE_2048);
    return 0;
}

static int e1000_setup_tx(struct e1000_device *dev) {
    u64 ring_phys = pmm_alloc_page();
    if (ring_phys == 0) {
        return -1;
    }
    dev->tx_ring = (struct e1000_tx_desc *)(uintptr_t)ring_phys;
    memset(dev->tx_ring, 0, PAGE_SIZE);

    for (unsigned i = 0; i < E1000_NUM_TX_DESC; i++) {
        u64 buf = pmm_alloc_page();
        if (buf == 0) {
            return -1;
        }
        dev->tx_buffers[i] = (u8 *)(uintptr_t)buf;
        dev->tx_ring[i].addr = buf;
        dev->tx_ring[i].status = E1000_TXD_STAT_DD;   /* free */
    }

    e1000_write(dev, E1000_REG_TDBAL, (u32)(ring_phys & 0xFFFFFFFFu));
    e1000_write(dev, E1000_REG_TDBAH, (u32)(ring_phys >> 32));
    e1000_write(dev, E1000_REG_TDLEN,
                E1000_NUM_TX_DESC * (u32)sizeof(struct e1000_tx_desc));
    e1000_write(dev, E1000_REG_TDH, 0);
    e1000_write(dev, E1000_REG_TDT, 0);
    dev->tx_cur = 0;

    e1000_write(dev, E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP);
    e1000_write(dev, E1000_REG_TIPG, 0x0060200Au);   /* recommended IPG */
    return 0;
}

/* ---- TX path -------------------------------------------------------------- */
static int e1000_tx_frame(struct e1000_device *dev, const u8 *data, u16 len) {
    if (len > E1000_RX_BUF_SIZE) {
        return -1;
    }
    unsigned i = dev->tx_cur;
    struct e1000_tx_desc *d = &dev->tx_ring[i];
    /* Wait for the previous descriptor at this slot to be reclaimed. */
    if (!(d->status & E1000_TXD_STAT_DD)) {
        /* Ring full in this minimal driver. */
        return -1;
    }
    memcpy(dev->tx_buffers[i], data, len);
    d->addr = (u64)(uintptr_t)dev->tx_buffers[i];
    d->length = len;
    d->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    d->status = 0;

    dev->tx_cur = (i + 1) % E1000_NUM_TX_DESC;
    e1000_write(dev, E1000_REG_TDT, dev->tx_cur);
    return 0;
}

int e1000_tx_raw(struct e1000_device *dev, const u8 *data, u16 len) {
    if (dev == NULL) {
        return -1;
    }
    return e1000_tx_frame(dev, data, len);
}

/* netif tx hook: nb->data points at the framed Ethernet packet. */
static int e1000_netif_tx(struct netif *nif, struct netbuf *nb) {
    struct e1000_device *dev = (struct e1000_device *)nif->driver;
    if (dev == NULL || !dev->initialized) {
        netbuf_free(nb);
        return -1;
    }
    int rc = e1000_tx_frame(dev, nb->data, (u16)netbuf_len(nb));
    netbuf_free(nb);
    return rc;
}

/* ---- RX path -------------------------------------------------------------- */
unsigned e1000_poll(struct e1000_device *dev) {
    unsigned delivered = 0;
    if (dev == NULL || !dev->initialized) {
        return 0;
    }
    while (1) {
        unsigned i = dev->rx_cur;
        struct e1000_rx_desc *d = &dev->rx_ring[i];
        if (!(d->status & E1000_RXD_STAT_DD)) {
            break;
        }
        u16 len = d->length;
        struct netbuf *nb = netbuf_alloc(len);
        if (nb != NULL) {
            netbuf_put_data(nb, dev->rx_buffers[i], len);
            netif_rx(&dev->nif, nb);
            delivered++;
        }
        /* Recycle the descriptor. */
        d->status = 0;
        e1000_write(dev, E1000_REG_RDT, i);
        dev->rx_cur = (i + 1) % E1000_NUM_RX_DESC;
    }
    return delivered;
}

/* ---- Bring-up ------------------------------------------------------------- */
static void e1000_reset(struct e1000_device *dev) {
    /* Mask all interrupts. */
    e1000_write(dev, E1000_REG_IMC, 0xFFFFFFFFu);
    /* Device reset. */
    u32 ctrl = e1000_read(dev, E1000_REG_CTRL);
    e1000_write(dev, E1000_REG_CTRL, ctrl | E1000_CTRL_RST);
    for (volatile int i = 0; i < 10000; i++) {
        /* brief settle */
    }
    /* Set link up + auto-speed detect. */
    ctrl = e1000_read(dev, E1000_REG_CTRL);
    e1000_write(dev, E1000_REG_CTRL, ctrl | E1000_CTRL_SLU | E1000_CTRL_ASDE);
    /* Mask interrupts again post-reset. */
    e1000_write(dev, E1000_REG_IMC, 0xFFFFFFFFu);
}

/* Bring up the controller against an already-mapped MMIO register window:
 * reset, read+program the MAC, build the RX/TX rings and register the netif.
 * Shared by the PCI path and the structural self-test (which supplies a
 * RAM-backed register window so no real device is required). */
int e1000_bringup(struct e1000_device *dev, volatile u8 *mmio,
                  const char *ifname) {
    dev->mmio = mmio;

    e1000_reset(dev);
    dev->has_eeprom = e1000_detect_eeprom(dev);
    e1000_read_mac(dev);
    e1000_set_mac(dev);

    if (e1000_setup_rx(dev) != 0 || e1000_setup_tx(dev) != 0) {
        return -1;
    }

    /* Register the interface. */
    memset(&dev->nif, 0, sizeof(dev->nif));
    strlcpy(dev->nif.name, ifname, NETIF_NAME_MAX);
    mac_copy(&dev->nif.mac, &dev->mac);
    dev->nif.mtu = ETH_MAX_DATA;
    dev->nif.flags = NETIF_FLAG_BROADCAST;
    dev->nif.tx = e1000_netif_tx;
    dev->nif.driver = dev;
    netif_register(&dev->nif);

    dev->initialized = 1;
    return 0;
}

int e1000_attach(struct e1000_device *dev, u64 mmio_phys) {
    /* Map the MMIO BAR (2 MiB window covering the register file). */
    if (vmm_map_mmio_2m(mmio_phys & ~LARGE_PAGE_MASK) != 0) {
        return -1;
    }
    return e1000_bringup(dev, (volatile u8 *)(uintptr_t)mmio_phys, "eth0");
}

int e1000_init(void) {
    const struct pci_device *pd =
        pci_find_by_id(E1000_VENDOR_ID, E1000_DEVICE_ID);
    if (pd == NULL) {
        return -1;   /* not present (loopback-only environment) */
    }
    pci_device_enable(pd);

    int is_mmio = 0;
    u64 bar0 = pci_device_bar(pd, 0, &is_mmio);
    if (bar0 == 0 || !is_mmio) {
        kernel_log_error("e1000: BAR0 is not memory-mapped");
        return -1;
    }

    memset(&g_e1000, 0, sizeof(g_e1000));
    if (e1000_attach(&g_e1000, bar0) != 0) {
        kernel_log_error("e1000: attach failed");
        return -1;
    }

    kdprintf("    e1000 MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
             g_e1000.mac.addr[0], g_e1000.mac.addr[1], g_e1000.mac.addr[2],
             g_e1000.mac.addr[3], g_e1000.mac.addr[4], g_e1000.mac.addr[5]);
    return 0;
}

struct e1000_device *e1000_primary(void) {
    return g_e1000.initialized ? &g_e1000 : NULL;
}
