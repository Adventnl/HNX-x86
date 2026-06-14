/* NVMe block device registration (deferred in Prompt 5). */
#ifndef MYOS_NVME_BLOCK_H
#define MYOS_NVME_BLOCK_H

/* Prompt 5 does not implement NVMe block I/O. Logs the deferral; registers no
 * block device (no faked success). */
void nvme_block_register_deferred(void);

#endif /* MYOS_NVME_BLOCK_H */
