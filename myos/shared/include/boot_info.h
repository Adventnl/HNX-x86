#ifndef MYOS_BOOT_INFO_H
#define MYOS_BOOT_INFO_H

#define MYOS_BOOT_INFO_MAGIC 0x4D594F53424F4FULL
#define MYOS_BOOT_INFO_VERSION 1

typedef unsigned char myos_u8;
typedef unsigned short myos_u16;
typedef unsigned int myos_u32;
typedef unsigned long long myos_u64;

struct boot_framebuffer {
    myos_u64 base;
    myos_u64 size;
    myos_u32 width;
    myos_u32 height;
    myos_u32 pixels_per_scanline;
    myos_u32 pitch;
    myos_u32 bits_per_pixel;
    myos_u32 pixel_format;
    myos_u32 red_mask;
    myos_u32 green_mask;
    myos_u32 blue_mask;
    myos_u32 reserved_mask;
};

struct boot_memory_map {
    myos_u64 map_base;
    myos_u64 map_size;
    myos_u64 descriptor_size;
    myos_u32 descriptor_version;
    myos_u32 reserved;
};

struct boot_kernel_info {
    myos_u64 kernel_base;
    myos_u64 kernel_size;
    myos_u64 kernel_entry;
};

struct boot_info {
    myos_u64 magic;
    myos_u32 version;
    myos_u32 size;

    struct boot_memory_map memory_map;
    struct boot_framebuffer framebuffer;
    struct boot_kernel_info kernel;

    myos_u64 rsdp_address;

    myos_u64 initramfs_base;
    myos_u64 initramfs_size;
};

#endif
