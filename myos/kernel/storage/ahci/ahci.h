/* AHCI (SATA) driver: register layout, port model, and public entry point.
 *
 * Flow: PCI probe (class 1, subclass 6, prog_if 1) -> map ABAR -> reset HBA ->
 * for each implemented port with a SATA disk: rebase (command list + FIS + one
 * command table in DMA memory), IDENTIFY for the sector count, then register a
 * block device whose read/write issue READ/WRITE DMA EXT. */
#ifndef MYOS_AHCI_H
#define MYOS_AHCI_H

#include "types.h"

/* ---- HBA memory-mapped registers ----------------------------------------- */
struct hba_port {
    volatile uint32_t clb, clbu, fb, fbu;
    volatile uint32_t is, ie, cmd, rsv0;
    volatile uint32_t tfd, sig, ssts, sctl;
    volatile uint32_t serr, sact, ci, sntf;
    volatile uint32_t fbs;
    volatile uint32_t rsv1[11];
    volatile uint32_t vendor[4];
};

struct hba_mem {
    volatile uint32_t cap, ghc, is, pi, vs;
    volatile uint32_t ccc_ctl, ccc_pts, em_loc, em_ctl, cap2, bohc;
    volatile uint8_t  rsv[0xA0 - 0x2C];
    volatile uint8_t  vendor[0x100 - 0xA0];
    struct hba_port   ports[32];
};

/* GHC / port command bits. */
#define AHCI_GHC_AE   (1u << 31)   /* AHCI enable */
#define AHCI_GHC_HR   (1u << 0)    /* HBA reset   */
#define HBA_PxCMD_ST  (1u << 0)
#define HBA_PxCMD_FRE (1u << 4)
#define HBA_PxCMD_FR  (1u << 14)
#define HBA_PxCMD_CR  (1u << 15)
#define HBA_PxTFD_BSY (1u << 7)
#define HBA_PxTFD_DRQ (1u << 3)
#define HBA_PxTFD_ERR (1u << 0)
#define HBA_PxIS_TFES (1u << 30)

#define AHCI_SIG_SATA 0x00000101u

/* ATA commands. */
#define ATA_CMD_READ_DMA_EXT  0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_IDENTIFY      0xEC

/* ---- DMA command structures ---------------------------------------------- */
struct hba_cmd_header {
    uint8_t  dw0_lo;     /* cfl(5) a(1) w(1) p(1) */
    uint8_t  dw0_hi;     /* r(1) b(1) c(1) rsv(1) pmp(4) */
    uint16_t prdtl;
    volatile uint32_t prdbc;
    uint32_t ctba, ctbau;
    uint32_t rsv[4];
} __attribute__((packed));

struct hba_prdt_entry {
    uint32_t dba, dbau, rsv;
    uint32_t dbc_i;      /* [21:0] byte count - 1, [31] interrupt */
} __attribute__((packed));

struct hba_cmd_table {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t rsv[48];
    struct hba_prdt_entry prdt[8];
} __attribute__((packed));

struct fis_reg_h2d {
    uint8_t fis_type;    /* 0x27 */
    uint8_t pmport_c;    /* [7] = command */
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0, lba1, lba2, device;
    uint8_t lba3, lba4, lba5, featureh;
    uint8_t countl, counth, icc, control;
    uint8_t rsv[4];
} __attribute__((packed));

/* ---- per-port software state --------------------------------------------- */
struct ahci_port {
    struct hba_port *regs;
    uint64_t cmdlist_phys;   /* command list (1 KiB) + FIS (256 B) page */
    uint64_t cmdtable_phys;  /* one command table */
    uint64_t buffer_phys;    /* DMA bounce buffer (1 page = 8 sectors) */
    uint64_t sector_count;
    int      index;
};

void ahci_init(void);     /* register the PCI driver + log online */

#endif /* MYOS_AHCI_H */
