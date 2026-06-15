/* Storage/driver production-foundation self-tests.
 *
 * Runs at boot in kernel context (kernel CR3 active), exercising the real
 * subsystems — driver lifecycle, PCI capability list, MSI/MSI-X discovery, the
 * AHCI block device (identify geometry, multi-sector round-trip, out-of-range
 * error handling), the NVMe foundation (controller discovered; block I/O is
 * honestly deferred), and the block-layer async + statistics foundations.
 *
 * All hardware access is guarded: a missing device makes its check fail loudly
 * rather than crashing. The AHCI I/O test uses the dedicated SCRATCH partition
 * (disk0p2), never the mounted HNXFS metadata on disk0p1.
 */
#include "storage_ext_tests.h"

#include "ktest.h"
#include "log.h"

#include "driver.h"
#include "driver_registry.h"
#include "device_power.h"

#include "pci.h"
#include "pci_device.h"
#include "pci_caps.h"
#include "msi.h"
#include "msix.h"

#include "block_registry.h"
#include "block_device.h"
#include "block_request.h"
#include "block_stats.h"

#include "string.h"

/* ---- mock driver used only by the lifecycle check ------------------------- */
static int sx_probe_calls;
static int sx_remove_calls;
static int sx_suspend_calls;
static int sx_resume_calls;

static int sx_probe(struct device *dev)   { (void)dev; sx_probe_calls++;   return 0; }
static int sx_remove(struct device *dev)  { (void)dev; sx_remove_calls++;  return 0; }
static int sx_suspend(struct device *dev) { (void)dev; sx_suspend_calls++; return 0; }
static int sx_resume(struct device *dev)  { (void)dev; sx_resume_calls++;  return 0; }

static struct driver g_sx_driver = {
    .name    = "storage-ext-mock",
    .type    = DEV_TYPE_STORAGE,
    .probe   = sx_probe,
    .remove  = sx_remove,
    .suspend = sx_suspend,
    .resume  = sx_resume,
};

/* ---- async completion callback state -------------------------------------- */
static volatile int      g_async_done;
static volatile int      g_async_status;
static struct block_request *g_async_seen;

static void async_complete(struct block_request *req, int status, void *cookie) {
    int *flag = (int *)cookie;
    if (flag) {
        *flag = 1;
    }
    g_async_seen   = req;
    g_async_status = status;
    g_async_done   = 1;
}

/* ========================================================================== */
void storage_ext_tests_run(void) {
    /* ---- driver lifecycle: register a mock storage driver and run a device
     *      through the full state machine. ----------------------------------- */
    {
        KT_BEGIN();
        struct device dev;
        device_init_struct(&dev, "sx-mockdev", DEV_TYPE_STORAGE);
        driver_register(&g_sx_driver);

        int bound = driver_probe(&dev);
        KT_CHECK(bound == DRIVER_PROBE_OK, "probe should claim the device");
        KT_CHECK(dev.state == DEVICE_STATE_ACTIVE, "device active after probe");
        KT_CHECK(dev.driver == &g_sx_driver, "driver bound after probe");
        KT_CHECK(sx_probe_calls == 1, "probe hook ran once");

        KT_CHECK(driver_suspend(&dev) == 0, "suspend ok");
        KT_CHECK(dev.state == DEVICE_STATE_SUSPENDED, "device suspended");
        KT_CHECK(device_power_get(&dev) == DEV_POWER_D3, "power D3 after suspend");

        KT_CHECK(driver_resume(&dev) == 0, "resume ok");
        KT_CHECK(dev.state == DEVICE_STATE_ACTIVE, "device active after resume");
        KT_CHECK(device_power_get(&dev) == DEV_POWER_D0, "power D0 after resume");

        KT_CHECK(driver_remove(&dev) == 0, "remove ok");
        KT_CHECK(dev.state == DEVICE_STATE_REMOVED, "device removed");
        KT_CHECK(dev.driver == NULL, "driver unbound after remove");
        KT_CHECK(sx_suspend_calls == 1 && sx_resume_calls == 1 && sx_remove_calls == 1,
                 "each lifecycle hook ran once");
        KT_RESULT("driver lifecycle");
    }

    /* ---- PCI capabilities: walk every PCI function and parse its capability
     *      list. A modern QEMU machine exposes PM/MSI/MSI-X/PCIe caps. -------- */
    {
        KT_BEGIN();
        int functions = pci_device_count();
        KT_CHECK(functions > 0, "PCI enumeration found functions");

        int with_caps = 0;
        for (int i = 0; i < functions; i++) {
            struct pci_device *d = (struct pci_device *)pci_device_at(i);
            if (!d) {
                continue;
            }
            if (pci_find_capability(d, PCI_CAP_ID_PM)   >= 0 ||
                pci_find_capability(d, PCI_CAP_ID_MSI)  >= 0 ||
                pci_find_capability(d, PCI_CAP_ID_MSIX) >= 0 ||
                pci_find_capability(d, PCI_CAP_ID_PCIE) >= 0) {
                with_caps++;
            }
        }
        KT_CHECK(with_caps > 0, "at least one function exposes a capability list");
        KT_RESULT("PCI capabilities");
    }

    /* ---- MSI foundation: probe msi_supported / msix_supported across the
     *      discovered functions. The AHCI/NVMe/xHCI controllers are MSI/MSI-X
     *      capable, so at least one must report support. -------------------- */
    {
        KT_BEGIN();
        int functions = pci_device_count();
        int msi_caps = 0, msix_caps = 0;
        for (int i = 0; i < functions; i++) {
            struct pci_device *d = (struct pci_device *)pci_device_at(i);
            if (!d) {
                continue;
            }
            if (msi_supported(d)) {
                msi_caps++;
            }
            if (msix_supported(d)) {
                msix_caps++;
            }
        }
        KT_CHECK((msi_caps + msix_caps) > 0,
                 "at least one function supports MSI or MSI-X");
        KT_RESULT("MSI foundation");
    }

    /* ---- AHCI identify: an AHCI-backed whole disk must exist with a sane
     *      geometry (512-byte sectors and a non-zero capacity). ------------- */
    struct block_device *disk    = block_get_device("disk0");
    struct block_device *scratch = block_get_device("disk0p2");
    {
        KT_BEGIN();
        KT_CHECK(disk != NULL, "disk0 (AHCI) present");
        if (disk) {
            KT_CHECK(disk->sector_size == BLOCK_SECTOR_SIZE, "512-byte sectors");
            KT_CHECK(disk->sector_count > 0, "non-zero sector count");
            KT_CHECK(disk->read != NULL && disk->write != NULL, "rw ops present");
        }
        KT_RESULT("AHCI identify");
    }

    /* ---- AHCI multi-sector read/write: stamp a known pattern across several
     *      sectors of the SCRATCH partition (disk0p2), read it back, verify.
     *      Never touches the HNXFS metadata on disk0p1. ---------------------- */
    {
        KT_BEGIN();
#define SX_NSEC 4u
        static uint8_t wbuf[SX_NSEC * BLOCK_SECTOR_SIZE];
        static uint8_t rbuf[SX_NSEC * BLOCK_SECTOR_SIZE];
        KT_CHECK(scratch != NULL, "disk0p2 scratch partition present");
        if (scratch && scratch->sector_count >= SX_NSEC) {
            for (uint32_t i = 0; i < sizeof(wbuf); i++) {
                wbuf[i] = (uint8_t)((i * 7u + 0x33u) & 0xFF);
            }
            memset(rbuf, 0, sizeof(rbuf));
            int wr = block_write(scratch, 0, wbuf, SX_NSEC);
            int rd = block_read(scratch, 0, rbuf, SX_NSEC);
            KT_CHECK(wr == 0, "multi-sector write succeeded");
            KT_CHECK(rd == 0, "multi-sector read succeeded");
            KT_CHECK(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0,
                     "read-back matches written pattern");
        }
        KT_RESULT("AHCI multi-sector read/write");
    }

    /* ---- AHCI error handling: a read past the end of the scratch partition
     *      must return an error (the partition wrapper bounds-checks the LBA)
     *      without crashing or touching the controller. --------------------- */
    {
        KT_BEGIN();
        static uint8_t ebuf[BLOCK_SECTOR_SIZE];
        if (scratch) {
            uint64_t bad_lba = scratch->sector_count + 16;   /* well past the end */
            int rc = block_read(scratch, bad_lba, ebuf, 1);
            KT_CHECK(rc < 0, "out-of-range read returns an error");
        } else {
            KT_CHECK(0, "disk0p2 scratch partition present");
        }
        KT_RESULT("AHCI error handling");
    }

    /* ---- NVMe identify: confirm the NVMe controller was discovered on the PCI
     *      bus (class 0x01, subclass 0x08). The controller bring-up parsed CAP/
     *      VS and allocated the admin-queue foundation at boot. -------------- */
    {
        KT_BEGIN();
        const struct pci_device *nvme = pci_find_by_class(0x01, 0x08);
        KT_CHECK(nvme != NULL, "NVMe controller discovered on PCI");
        if (nvme) {
            int is_mmio = 0;
            uint64_t bar0 = pci_device_bar(nvme, 0, &is_mmio);
            KT_CHECK(bar0 != 0 && is_mmio, "NVMe BAR0 is a valid MMIO region");
        }
        KT_RESULT("NVMe identify");
    }

    /* ---- NVMe read/write: honest deferred blocker. NVMe block I/O is NOT
     *      implemented. Confirm the deferral is clean: no block device claims to
     *      be NVMe-backed (no "nvme*" block device registered). --------------- */
    {
        KT_BEGIN();
        int nvme_block_devs = 0;
        int n = block_device_count();
        for (int i = 0; i < n; i++) {
            struct block_device *d = block_device_at(i);
            if (d && strncmp(d->name, "nvme", 4) == 0) {
                nvme_block_devs++;
            }
        }
        KT_CHECK(nvme_block_devs == 0,
                 "no NVMe block device falsely claims to work (deferred)");
        KT_RESULT("NVMe read/write deferred (no fake block device)");
    }

    /* ---- block async foundation: submit an async request carrying a callback,
     *      confirm the callback ran and the data is correct. ----------------- */
    {
        KT_BEGIN();
        static uint8_t async_buf[BLOCK_SECTOR_SIZE];
        int cb_flag = 0;
        g_async_done   = 0;
        g_async_status = -1;
        g_async_seen   = NULL;

        if (scratch) {
            /* Seed a known sector synchronously, then read it back via async. */
            static uint8_t seed[BLOCK_SECTOR_SIZE];
            for (uint32_t i = 0; i < BLOCK_SECTOR_SIZE; i++) {
                seed[i] = (uint8_t)(i ^ 0xA5);
            }
            int sw = block_write(scratch, 1, seed, 1);
            KT_CHECK(sw == 0, "async test seed write ok");

            memset(async_buf, 0, sizeof(async_buf));
            struct block_request req;
            memset(&req, 0, sizeof(req));
            req.dev         = scratch;
            req.op          = BLOCK_OP_READ;
            req.lba         = 1;
            req.count       = 1;
            req.buffer      = async_buf;
            req.on_complete = async_complete;
            req.cookie      = &cb_flag;

            int rc = block_request_submit_async(&req);
            KT_CHECK(rc == 0, "async submit accepted");
            KT_CHECK(g_async_done == 1, "completion callback ran");
            KT_CHECK(cb_flag == 1, "callback received its cookie");
            KT_CHECK(g_async_seen == &req, "callback received the request");
            KT_CHECK(g_async_status == 0, "async transfer status ok");
            KT_CHECK(req.status == 0, "request status recorded");
            KT_CHECK(memcmp(async_buf, seed, BLOCK_SECTOR_SIZE) == 0,
                     "async read returned the correct data");
        } else {
            KT_CHECK(0, "disk0p2 scratch partition present");
        }
        KT_RESULT("block async foundation");
    }

    /* ---- block stats: perform some I/O and confirm the global counters
     *      advanced by the expected amounts. -------------------------------- */
    {
        KT_BEGIN();
        static uint8_t sbuf[2 * BLOCK_SECTOR_SIZE];
        struct block_stats before, after;
        block_get_stats(&before);

        if (scratch && scratch->sector_count >= 2) {
            memset(sbuf, 0xC3, sizeof(sbuf));
            int wr = block_write(scratch, 0, sbuf, 2);   /* +1 write, +2 wsec */
            int rd = block_read(scratch, 0, sbuf, 2);    /* +1 read,  +2 rsec */
            KT_CHECK(wr == 0 && rd == 0, "stats I/O succeeded");

            block_get_stats(&after);
            KT_CHECK(after.writes       >= before.writes + 1,       "write count advanced");
            KT_CHECK(after.reads        >= before.reads + 1,        "read count advanced");
            KT_CHECK(after.write_sectors >= before.write_sectors + 2, "write sectors advanced");
            KT_CHECK(after.read_sectors  >= before.read_sectors + 2,  "read sectors advanced");
        } else {
            KT_CHECK(0, "disk0p2 scratch partition present");
        }
        KT_RESULT("block stats");
    }

    /* ---- final summary marker. -------------------------------------------- */
    kernel_log_ok("Storage/driver production foundation online");
#undef SX_NSEC
}
