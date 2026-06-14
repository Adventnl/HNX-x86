/* AHCI HBA controller bring-up. */
#ifndef MYOS_AHCI_CONTROLLER_H
#define MYOS_AHCI_CONTROLLER_H

#include "types.h"

/* Map the ABAR, reset/enable the HBA, scan implemented ports, register a block
 * device per SATA disk. Returns the number of disks registered. */
int ahci_controller_init(uint64_t abar);

#endif /* MYOS_AHCI_CONTROLLER_H */
