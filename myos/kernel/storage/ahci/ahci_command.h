/* AHCI command issue (slot 0, polled). */
#ifndef MYOS_AHCI_COMMAND_H
#define MYOS_AHCI_COMMAND_H

#include "ahci.h"

/* Issue READ/WRITE DMA EXT for `count` (<=8) sectors at `lba`, transferring
 * through the port's bounce buffer. Returns 0 on success. */
int ahci_command_rw(struct ahci_port *port, uint64_t lba, uint32_t count, int write);

/* Issue IDENTIFY DEVICE into the bounce buffer. Returns 0 on success. */
int ahci_command_identify(struct ahci_port *port);

#endif /* MYOS_AHCI_COMMAND_H */
