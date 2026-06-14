/* AHCI block-device glue. */
#ifndef MYOS_AHCI_DISK_H
#define MYOS_AHCI_DISK_H

#include "ahci.h"

/* Register a block device ("diskN") backed by `port`. Returns 0 on success. */
int ahci_disk_register(struct ahci_port *port);

#endif /* MYOS_AHCI_DISK_H */
