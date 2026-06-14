/* MBR parser. */
#include "mbr.h"
#include "partition.h"
#include "block_device.h"
#include "gpt.h"
#include "string.h"
#include "log.h"

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int mbr_parse(struct block_device *device) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    if (block_read(device, 0, sector, 1) != 0) {
        return -1;
    }
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return -1;
    }

    int registered = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = &sector[446 + i * 16];
        uint8_t type = e[4];
        uint32_t start = rd32(&e[8]);
        uint32_t count = rd32(&e[12]);
        if (type == 0xEE) {
            return gpt_parse(device);   /* protective MBR -> defer to GPT */
        }
        if (type == 0 || count == 0) {
            continue;
        }
        if (partition_register(device, i + 1, start, count, type) == 0) {
            registered++;
        }
    }
    return registered > 0 ? 0 : -1;
}
