/* xHCI register layout, TRB and context definitions (xHCI spec 1.2).
 *
 * The controller exposes four register banks inside BAR0:
 *   capability  @ base
 *   operational @ base + CAPLENGTH
 *   runtime     @ base + RTSOFF
 *   doorbell    @ base + DBOFF
 * Memory-resident structures (DCBAA, command/event rings, ERST, device/input
 * contexts, scratchpad) are DMA buffers allocated from the PMM (phys == identity
 * virt) and referenced by the controller via physical addresses. */
#ifndef MYOS_XHCI_REGS_H
#define MYOS_XHCI_REGS_H

#include "types.h"

/* ---- Capability registers (offsets from BAR0 base) ------------------------ */
#define XHCI_CAP_CAPLENGTH   0x00   /* u8  */
#define XHCI_CAP_HCIVERSION  0x02   /* u16 */
#define XHCI_CAP_HCSPARAMS1  0x04   /* u32 */
#define XHCI_CAP_HCSPARAMS2  0x08   /* u32 */
#define XHCI_CAP_HCSPARAMS3  0x0C   /* u32 */
#define XHCI_CAP_HCCPARAMS1  0x10   /* u32 */
#define XHCI_CAP_DBOFF       0x14   /* u32 */
#define XHCI_CAP_RTSOFF      0x18   /* u32 */
#define XHCI_CAP_HCCPARAMS2  0x1C   /* u32 */

/* HCSPARAMS1 fields. */
#define XHCI_HCS1_MAXSLOTS(x)  ((x) & 0xFF)
#define XHCI_HCS1_MAXINTRS(x)  (((x) >> 8) & 0x7FF)
#define XHCI_HCS1_MAXPORTS(x)  (((x) >> 24) & 0xFF)

/* HCSPARAMS2: max scratchpad buffers = Hi[25:21] << 5 | Lo[31:27]. */
#define XHCI_HCS2_MAXSCRATCH(x) ((((x) >> 21) & 0x1F) << 5 | (((x) >> 27) & 0x1F))

/* HCCPARAMS1 fields. */
#define XHCI_HCC1_AC64(x)   ((x) & 0x1)
#define XHCI_HCC1_CSZ(x)    (((x) >> 2) & 0x1)
#define XHCI_HCC1_XECP(x)   (((x) >> 16) & 0xFFFF)

/* ---- Operational registers (offsets from base + CAPLENGTH) ---------------- */
#define XHCI_OP_USBCMD       0x00
#define XHCI_OP_USBSTS       0x04
#define XHCI_OP_PAGESIZE     0x08
#define XHCI_OP_DNCTRL       0x14
#define XHCI_OP_CRCR         0x18   /* 64-bit */
#define XHCI_OP_DCBAAP       0x30   /* 64-bit */
#define XHCI_OP_CONFIG       0x38
#define XHCI_OP_PORTS        0x400  /* port register sets, 0x10 apart        */
#define XHCI_PORT_STRIDE     0x10
#define XHCI_PORT_PORTSC     0x00

/* USBCMD bits. */
#define XHCI_CMD_RUN         (1u << 0)
#define XHCI_CMD_HCRST       (1u << 1)
#define XHCI_CMD_INTE        (1u << 2)
#define XHCI_CMD_HSEE        (1u << 3)

/* USBSTS bits. */
#define XHCI_STS_HCH         (1u << 0)   /* HC halted              */
#define XHCI_STS_HSE         (1u << 2)   /* host system error      */
#define XHCI_STS_EINT        (1u << 3)   /* event interrupt        */
#define XHCI_STS_PCD         (1u << 4)   /* port change detect     */
#define XHCI_STS_CNR         (1u << 11)  /* controller not ready   */

/* PORTSC bits. */
#define XHCI_PORTSC_CCS      (1u << 0)   /* current connect status */
#define XHCI_PORTSC_PED      (1u << 1)   /* port enabled           */
#define XHCI_PORTSC_OCA      (1u << 3)   /* over-current           */
#define XHCI_PORTSC_PR       (1u << 4)   /* port reset             */
#define XHCI_PORTSC_PP       (1u << 9)   /* port power             */
#define XHCI_PORTSC_SPEED(x) (((x) >> 10) & 0xF)
#define XHCI_PORTSC_CSC      (1u << 17)  /* connect status change  */
#define XHCI_PORTSC_PEC      (1u << 18)
#define XHCI_PORTSC_PRC      (1u << 21)  /* port reset change      */
/* Write-1-to-clear change bits we must preserve-mask when writing PORTSC. */
#define XHCI_PORTSC_RW1CS    (XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | (1u<<19) | \
                              (1u<<20) | XHCI_PORTSC_PRC | (1u<<22) | (1u<<23))

/* USB speed ids as reported in PORTSC[13:10] (qemu-xhci default protocol map). */
#define XHCI_SPEED_FULL      1
#define XHCI_SPEED_LOW       2
#define XHCI_SPEED_HIGH      3
#define XHCI_SPEED_SUPER     4

/* ---- Runtime registers (offsets from base + RTSOFF) ----------------------- */
#define XHCI_RT_IR0          0x20   /* interrupter 0 register set            */
#define XHCI_IR_IMAN         0x00
#define XHCI_IR_IMOD         0x04
#define XHCI_IR_ERSTSZ       0x08
#define XHCI_IR_ERSTBA       0x10   /* 64-bit */
#define XHCI_IR_ERDP         0x18   /* 64-bit */
#define XHCI_IMAN_IP         (1u << 0)
#define XHCI_IMAN_IE         (1u << 1)
#define XHCI_ERDP_EHB        (1u << 3)   /* event handler busy (RW1C) */

/* ---- Transfer Request Block ----------------------------------------------- */
struct xhci_trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed));

/* control[15:10] = TRB type. */
#define XHCI_TRB_TYPE(t)     (((t) & 0x3F) << 10)
#define XHCI_TRB_GET_TYPE(c) (((c) >> 10) & 0x3F)
#define XHCI_TRB_CYCLE       (1u << 0)

/* TRB types. */
#define XHCI_TRB_NORMAL          1
#define XHCI_TRB_SETUP           2
#define XHCI_TRB_DATA            3
#define XHCI_TRB_STATUS          4
#define XHCI_TRB_LINK            6
#define XHCI_TRB_ENABLE_SLOT     9
#define XHCI_TRB_DISABLE_SLOT    10
#define XHCI_TRB_ADDRESS_DEVICE  11
#define XHCI_TRB_CONFIG_EP       12
#define XHCI_TRB_EVAL_CONTEXT    13
#define XHCI_TRB_NOOP_CMD        23
#define XHCI_TRB_TRANSFER_EVENT  32
#define XHCI_TRB_CMD_COMPLETION  33
#define XHCI_TRB_PORT_STATUS     34

/* Completion codes (event status[31:24]). */
#define XHCI_CC_SUCCESS          1
#define XHCI_CC(status)          (((status) >> 24) & 0xFF)
/* Command completion event: slot id in control[31:24]. */
#define XHCI_EVENT_SLOT(control) (((control) >> 24) & 0xFF)

/* Setup-stage / link / control TRB helper bits. */
#define XHCI_TRB_IOC         (1u << 5)   /* interrupt on completion   */
#define XHCI_TRB_IDT         (1u << 6)   /* immediate data            */
#define XHCI_TRB_CHAIN       (1u << 4)
#define XHCI_TRB_ENT         (1u << 1)
#define XHCI_TRB_TOGGLE      (1u << 1)   /* link TRB: toggle cycle    */
#define XHCI_TRB_DIR_IN      (1u << 16)  /* data/status stage dir     */
#define XHCI_TRT_NO_DATA     0
#define XHCI_TRT_OUT         2
#define XHCI_TRT_IN          3

/* ---- Event Ring Segment Table entry --------------------------------------- */
struct xhci_erst_entry {
    uint64_t ring_base;
    uint32_t ring_size;
    uint32_t reserved;
} __attribute__((packed));

/* TRBs per ring page (4096 / 16). */
#define XHCI_RING_TRBS       256

#endif /* MYOS_XHCI_REGS_H */
