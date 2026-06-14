/* AHCI port bring-up: stop, rebase DMA structures, start, IDENTIFY. */
#ifndef MYOS_AHCI_PORT_H
#define MYOS_AHCI_PORT_H

#include "ahci.h"

/* Returns 1 if a SATA disk is present on this port. */
int ahci_port_has_disk(struct hba_port *p);

/* Allocate DMA memory, rebase the port, run IDENTIFY, fill *out. 0 on success. */
int ahci_port_init(struct hba_port *regs, int index, struct ahci_port *out);

#endif /* MYOS_AHCI_PORT_H */
