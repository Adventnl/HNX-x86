/* AHCI port initialization. */
#include "ahci_port.h"
#include "ahci_command.h"
#include "pmm.h"
#include "memory_layout.h"
#include "string.h"

#define SPIN_LIMIT 10000000u

int ahci_port_has_disk(struct hba_port *p) {
    uint32_t ssts = p->ssts;
    uint8_t det = ssts & 0x0F;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    if (det != 3 || ipm != 1) {
        return 0;
    }
    return p->sig == AHCI_SIG_SATA;
}

static void stop_port(struct hba_port *p) {
    p->cmd &= ~HBA_PxCMD_ST;
    p->cmd &= ~HBA_PxCMD_FRE;
    uint32_t spin = 0;
    while ((p->cmd & (HBA_PxCMD_FR | HBA_PxCMD_CR)) && spin < SPIN_LIMIT) {
        spin++;
    }
}

static void start_port(struct hba_port *p) {
    uint32_t spin = 0;
    while ((p->cmd & HBA_PxCMD_CR) && spin < SPIN_LIMIT) {
        spin++;
    }
    p->cmd |= HBA_PxCMD_FRE;
    p->cmd |= HBA_PxCMD_ST;
}

int ahci_port_init(struct hba_port *regs, int index, struct ahci_port *out) {
    /* Three DMA pages: [cmd list + FIS], [cmd table], [bounce buffer]. */
    uint64_t page_cl = pmm_alloc_page();
    uint64_t page_ct = pmm_alloc_page();
    uint64_t page_buf = pmm_alloc_page();
    if (page_cl == PMM_INVALID_PAGE || page_ct == PMM_INVALID_PAGE ||
        page_buf == PMM_INVALID_PAGE) {
        return -1;
    }
    memset((void *)(uintptr_t)page_cl, 0, PAGE_SIZE);
    memset((void *)(uintptr_t)page_ct, 0, PAGE_SIZE);

    out->regs = regs;
    out->index = index;
    out->cmdlist_phys = page_cl;             /* 1 KiB command list at +0 */
    out->buffer_phys = page_buf;
    out->cmdtable_phys = page_ct;

    stop_port(regs);

    /* Command list base + received-FIS base (FIS at +1024 in the same page). */
    regs->clb = (uint32_t)(page_cl & 0xFFFFFFFF);
    regs->clbu = (uint32_t)(page_cl >> 32);
    uint64_t fis_phys = page_cl + 1024;
    regs->fb = (uint32_t)(fis_phys & 0xFFFFFFFF);
    regs->fbu = (uint32_t)(fis_phys >> 32);

    /* Command header 0 -> our single command table. */
    struct hba_cmd_header *hdr = (struct hba_cmd_header *)(uintptr_t)page_cl;
    hdr->ctba = (uint32_t)(page_ct & 0xFFFFFFFF);
    hdr->ctbau = (uint32_t)(page_ct >> 32);

    regs->serr = (uint32_t)-1;   /* clear errors */
    regs->is = (uint32_t)-1;

    start_port(regs);

    /* IDENTIFY for the sector count (LBA48 at word 100, else LBA28 at word 60). */
    out->sector_count = 0;
    if (ahci_command_identify(out) == 0) {
        const uint16_t *id = (const uint16_t *)(uintptr_t)page_buf;
        uint64_t lba48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                         ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
        uint32_t lba28 = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
        out->sector_count = lba48 ? lba48 : lba28;
    }
    return 0;
}
