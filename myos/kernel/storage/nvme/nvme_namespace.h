/* NVMe namespace probing (deferred in Prompt 5). */
#ifndef MYOS_NVME_NAMESPACE_H
#define MYOS_NVME_NAMESPACE_H

struct nvme_controller;

/* Probe namespaces. Prompt 5 defers block I/O: logs the deferral and does not
 * register a block device. */
void nvme_namespace_probe(struct nvme_controller *ctrl);

#endif /* MYOS_NVME_NAMESPACE_H */
