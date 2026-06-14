/* AHCI command construction + polled completion. */
#include "ahci_command.h"
#include "string.h"

#define SPIN_LIMIT 10000000u

static int wait_not_busy(struct hba_port *p) {
    uint32_t spin = 0;
    while ((p->tfd & (HBA_PxTFD_BSY | HBA_PxTFD_DRQ)) && spin < SPIN_LIMIT) {
        spin++;
    }
    return spin < SPIN_LIMIT ? 0 : -1;
}

/* Common command issue: set up the command header + table + one PRDT entry,
 * fill the H2D FIS, fire slot 0, poll for completion. */
static int issue(struct ahci_port *port, uint8_t command, uint64_t lba,
                 uint32_t count, uint32_t bytes, int write, int use_lba) {
    struct hba_port *p = port->regs;
    if (wait_not_busy(p) != 0) {
        return -1;
    }
    p->is = (uint32_t)-1;   /* clear pending interrupt status */

    struct hba_cmd_header *hdr = (struct hba_cmd_header *)(uintptr_t)port->cmdlist_phys;
    hdr->dw0_lo = (uint8_t)((sizeof(struct fis_reg_h2d) / 4) & 0x1F);  /* CFL */
    if (write) {
        hdr->dw0_lo |= (1u << 6);   /* W bit */
    } else {
        hdr->dw0_lo &= ~(1u << 6);
    }
    hdr->dw0_hi = 0;
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    struct hba_cmd_table *tbl = (struct hba_cmd_table *)(uintptr_t)port->cmdtable_phys;
    memset(tbl, 0, sizeof(struct hba_cmd_table));
    tbl->prdt[0].dba = (uint32_t)(port->buffer_phys & 0xFFFFFFFF);
    tbl->prdt[0].dbau = (uint32_t)(port->buffer_phys >> 32);
    tbl->prdt[0].dbc_i = (bytes - 1) & 0x3FFFFF;   /* byte count - 1 */

    struct fis_reg_h2d *fis = (struct fis_reg_h2d *)tbl->cfis;
    fis->fis_type = 0x27;
    fis->pmport_c = 0x80;   /* command */
    fis->command = command;
    if (use_lba) {
        fis->lba0 = (uint8_t)lba;
        fis->lba1 = (uint8_t)(lba >> 8);
        fis->lba2 = (uint8_t)(lba >> 16);
        fis->device = 0x40;          /* LBA mode */
        fis->lba3 = (uint8_t)(lba >> 24);
        fis->lba4 = (uint8_t)(lba >> 32);
        fis->lba5 = (uint8_t)(lba >> 40);
        fis->countl = (uint8_t)count;
        fis->counth = (uint8_t)(count >> 8);
    } else {
        fis->device = 0;
    }

    p->ci = 1;   /* issue slot 0 */

    uint32_t spin = 0;
    while ((p->ci & 1) && spin < SPIN_LIMIT) {
        if (p->is & HBA_PxIS_TFES) {
            return -1;
        }
        spin++;
    }
    if (spin >= SPIN_LIMIT) {
        return -1;
    }
    if (p->tfd & HBA_PxTFD_ERR) {
        return -1;
    }
    return 0;
}

int ahci_command_rw(struct ahci_port *port, uint64_t lba, uint32_t count, int write) {
    if (count == 0 || count > 8) {
        return -1;
    }
    uint8_t cmd = write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
    return issue(port, cmd, lba, count, count * 512u, write, 1);
}

int ahci_command_identify(struct ahci_port *port) {
    return issue(port, ATA_CMD_IDENTIFY, 0, 0, 512, 0, 0);
}
