# Block Layer

`struct block_device` { name, sector_count, sector_size (512), read/write ops,
driver_data }. Drivers (AHCI, partitions) fill it in and `block_register_device`.

`block_read/block_write(dev, lba, buf, count)` loop sector-by-sector through the
**block cache**. The cache (`block_cache.c`) is direct-mapped, 64 lines of 512
bytes keyed by (device, lba), **write-through**: writes hit the device first then
update the cached copy (so a line is never left dirty). Stats: hits, misses,
writes, evictions, dirty (0 under write-through). `block_cache_flush_all()` is a
no-op placeholder for the future write-back mode.

`block_request.c` is a thin synchronous request descriptor
(`struct block_request` + `block_request_submit`) — the foundation for an async
queue later.

`block_registry.c` owns the device list + bring-up (`block_init` initializes the
cache). Registered devices are mirrored into the driver-core registry as
`DEV_TYPE_BLOCK` so `devices` lists them; `blocks()` enumerates the block layer
directly for `lsblk`.

Markers: `[OK] Block layer online`, `[OK] Block cache online`,
`[PASS] block cache`, `[PASS] partition parser`.

## Partitions

`partition_scan_all()` parses each whole disk: `gpt_parse` (LBA1 "EFI PART"
header + entry array) is tried first, falling back to `mbr_parse` (0x55AA
signature + four 16-byte entries; a 0xEE protective entry defers to GPT). Each
partition becomes a child `block_device` named `<disk>pN` whose raw read/write
forward to the parent with a fixed LBA offset (the cache sits above, keyed by the
partition device). Example: `disk0`, `disk0p1`, `disk0p2`.
