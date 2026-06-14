/* GPT parser: validate the LBA1 header, walk the partition entry array. */
#include "gpt.h"
#include "partition.h"
#include "block_device.h"
#include "string.h"

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static int guid_is_zero(const uint8_t *g) {
    for (int i = 0; i < 16; i++) {
        if (g[i]) return 0;
    }
    return 1;
}

int gpt_parse(struct block_device *device) {
    uint8_t hdr[BLOCK_SECTOR_SIZE];
    if (block_read(device, 1, hdr, 1) != 0) {
        return -1;
    }
    if (memcmp(hdr, "EFI PART", 8) != 0) {
        return -1;
    }
    uint64_t entry_lba = rd64(&hdr[72]);
    uint32_t num_entries = rd32(&hdr[80]);
    uint32_t entry_size = rd32(&hdr[84]);
    if (entry_size < 128 || entry_size > BLOCK_SECTOR_SIZE) {
        return -1;
    }
    uint32_t per_sector = BLOCK_SECTOR_SIZE / entry_size;
    if (num_entries > 128) {
        num_entries = 128;
    }

    uint8_t sector[BLOCK_SECTOR_SIZE];
    int registered = 0;
    uint32_t idx = 0;
    for (uint32_t s = 0; idx < num_entries; s++) {
        if (block_read(device, entry_lba + s, sector, 1) != 0) {
            break;
        }
        for (uint32_t k = 0; k < per_sector && idx < num_entries; k++, idx++) {
            const uint8_t *e = &sector[k * entry_size];
            if (guid_is_zero(e)) {
                continue;   /* type GUID zero -> unused */
            }
            uint64_t first = rd64(&e[32]);
            uint64_t last = rd64(&e[40]);
            if (last < first) {
                continue;
            }
            if (partition_register(device, (int)idx + 1, first, last - first + 1, 0x83) == 0) {
                registered++;
            }
        }
    }
    return registered > 0 ? 0 : -1;
}
