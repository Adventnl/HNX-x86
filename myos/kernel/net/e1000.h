/* Intel e1000 (82540EM / QEMU "e1000") NIC driver.
 *
 * PCI probe (vendor 0x8086, device 0x100E) -> map the MMIO BAR -> reset ->
 * read the MAC from RAL/RAH (or EEPROM) -> set up RX/TX descriptor rings ->
 * register a netif whose tx writes into the TX ring. In QEMU without a backend
 * the link never comes up, but the descriptor/register bring-up is real; the
 * protocol tests run over loopback regardless.
 */
#ifndef MYOS_NET_E1000_H
#define MYOS_NET_E1000_H

#include "types.h"
#include "net_if.h"

#define E1000_VENDOR_ID 0x8086u
#define E1000_DEVICE_ID 0x100Eu   /* 82540EM, QEMU default e1000 */

/* ---- Register offsets (byte offsets into the MMIO BAR) -------------------- */
#define E1000_REG_CTRL    0x0000u   /* Device Control            */
#define E1000_REG_STATUS  0x0008u   /* Device Status             */
#define E1000_REG_EECD    0x0010u   /* EEPROM/Flash Control      */
#define E1000_REG_EERD    0x0014u   /* EEPROM Read               */
#define E1000_REG_ICR     0x00C0u   /* Interrupt Cause Read      */
#define E1000_REG_IMS     0x00D0u   /* Interrupt Mask Set        */
#define E1000_REG_IMC     0x00D8u   /* Interrupt Mask Clear      */
#define E1000_REG_RCTL    0x0100u   /* Receive Control           */
#define E1000_REG_TCTL    0x0400u   /* Transmit Control          */
#define E1000_REG_TIPG    0x0410u   /* Transmit Inter Packet Gap */

#define E1000_REG_RDBAL   0x2800u   /* RX Descriptor Base Low    */
#define E1000_REG_RDBAH   0x2804u   /* RX Descriptor Base High   */
#define E1000_REG_RDLEN   0x2808u   /* RX Descriptor Length      */
#define E1000_REG_RDH     0x2810u   /* RX Descriptor Head        */
#define E1000_REG_RDT     0x2818u   /* RX Descriptor Tail        */

#define E1000_REG_TDBAL   0x3800u   /* TX Descriptor Base Low    */
#define E1000_REG_TDBAH   0x3804u   /* TX Descriptor Base High   */
#define E1000_REG_TDLEN   0x3808u   /* TX Descriptor Length      */
#define E1000_REG_TDH     0x3810u   /* TX Descriptor Head        */
#define E1000_REG_TDT     0x3818u   /* TX Descriptor Tail        */

#define E1000_REG_RAL0    0x5400u   /* Receive Address Low  [0]  */
#define E1000_REG_RAH0    0x5404u   /* Receive Address High [0]  */

/* CTRL bits. */
#define E1000_CTRL_RST    (1u << 26)
#define E1000_CTRL_ASDE   (1u << 5)
#define E1000_CTRL_SLU    (1u << 6)

/* RCTL bits. */
#define E1000_RCTL_EN     (1u << 1)
#define E1000_RCTL_BAM    (1u << 15)   /* broadcast accept */
#define E1000_RCTL_SECRC  (1u << 26)   /* strip CRC        */
#define E1000_RCTL_BSIZE_2048 0u       /* default 2048-byte buffers */

/* TCTL bits. */
#define E1000_TCTL_EN     (1u << 1)
#define E1000_TCTL_PSP    (1u << 3)    /* pad short packets */

/* TX descriptor command bits. */
#define E1000_TXD_CMD_EOP (1u << 0)    /* end of packet */
#define E1000_TXD_CMD_IFCS (1u << 1)   /* insert FCS     */
#define E1000_TXD_CMD_RS  (1u << 3)    /* report status  */
#define E1000_TXD_STAT_DD (1u << 0)    /* descriptor done */

/* RX descriptor status bits. */
#define E1000_RXD_STAT_DD  (1u << 0)
#define E1000_RXD_STAT_EOP (1u << 1)

#define E1000_NUM_RX_DESC 32u
#define E1000_NUM_TX_DESC 32u
#define E1000_RX_BUF_SIZE 2048u

/* Legacy descriptor formats (little-endian, hardware layout). */
struct e1000_rx_desc {
    u64 addr;
    u16 length;
    u16 checksum;
    u8  status;
    u8  errors;
    u16 special;
} __attribute__((packed));

struct e1000_tx_desc {
    u64 addr;
    u16 length;
    u8  cso;
    u8  cmd;
    u8  status;
    u8  css;
    u16 special;
} __attribute__((packed));

struct e1000_device {
    volatile u8 *mmio;            /* mapped BAR0 */
    struct mac_addr mac;
    int  has_eeprom;

    struct e1000_rx_desc *rx_ring;
    struct e1000_tx_desc *tx_ring;
    u8   *rx_buffers[E1000_NUM_RX_DESC];
    u8   *tx_buffers[E1000_NUM_TX_DESC];
    unsigned rx_cur;
    unsigned tx_cur;

    struct netif nif;
    int  initialized;
};

/* PCI-driven probe + init. Returns 0 if a device was found and brought up, -1
 * if none present (loopback-only environment). Registers the netif on success. */
int  e1000_init(void);

/* Direct init from a known MMIO base (used when a probe already resolved BAR0).
 * Returns 0 on success. */
int  e1000_attach(struct e1000_device *dev, u64 mmio_phys);

/* Bring up the controller against an already-mapped register window. Used by
 * e1000_attach and by the structural self-test (RAM-backed register window so
 * no real hardware is needed). Returns 0 on success. */
int  e1000_bringup(struct e1000_device *dev, volatile u8 *mmio,
                   const char *ifname);

/* Transmit a raw frame straight through the TX ring (test/diagnostic seam).
 * Returns 0 on success, -1 if the ring slot is busy or the frame is too large. */
int  e1000_tx_raw(struct e1000_device *dev, const u8 *data, u16 len);

struct e1000_device *e1000_primary(void);

/* Poll the RX ring and push received frames up the stack (call from an IRQ
 * handler or a poll loop). Returns the number of frames delivered. */
unsigned e1000_poll(struct e1000_device *dev);

#endif /* MYOS_NET_E1000_H */
