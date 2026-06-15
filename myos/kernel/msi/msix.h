/* MSI-X foundation. MSI-X keeps its vector table in an MMIO BAR rather than in
 * config space, allowing many independent vectors per function. MyOS parses and
 * programs the table; routing into live interrupt handlers is layered on top by
 * the controller drivers that opt in. */
#ifndef MYOS_MSIX_H
#define MYOS_MSIX_H

#include "types.h"

struct pci_device;

int  msix_supported(struct pci_device *dev);

/* Map the MSI-X vector table BAR into kernel MMIO space. Returns 0 on success,
 * negative on failure (unsupported / BAR unresolved). */
int  msix_map_table(struct pci_device *dev);

/* Program one table entry to deliver to `vector` on the BSP and unmask it.
 * Requires msix_map_table() to have succeeded first. */
int  msix_enable_vector(struct pci_device *dev, uint16_t table_index, uint8_t vector);

void msix_disable(struct pci_device *dev);

/* Table size (number of vectors) for `dev`, or -1 if unsupported. */
int  msix_table_size(struct pci_device *dev);

#endif /* MYOS_MSIX_H */
