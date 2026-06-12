/* Global Descriptor Table setup. */
#include "gdt.h"
#include "cpu.h"

/* A normal 8-byte descriptor. */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;   /* limit_high (4) + flags (4) */
    uint8_t  base_high;
} __attribute__((packed));

/* The 64-bit TSS descriptor occupies two 8-byte slots. */
struct tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

/* 5 code/data descriptors + 2 slots for the TSS = 7 * 8 bytes. */
static struct gdt_entry gdt[7] __attribute__((aligned(16)));

static struct x86_descriptor_ptr gdtr;

static void set_entry(int index, uint8_t access, uint8_t granularity) {
    gdt[index].limit_low   = 0;
    gdt[index].base_low    = 0;
    gdt[index].base_mid    = 0;
    gdt[index].access      = access;
    gdt[index].granularity = granularity;
    gdt[index].base_high   = 0;
}

void gdt_init(void) {
    /* 0x00: null */
    set_entry(0, 0x00, 0x00);
    /* 0x08: kernel code  (present, ring0, code, exec/read) + long mode (L=1) */
    set_entry(1, 0x9A, 0x20);
    /* 0x10: kernel data  (present, ring0, data, read/write)                  */
    set_entry(2, 0x92, 0x00);
    /* 0x18: user data    (present, ring3, data, read/write)                  */
    set_entry(3, 0xF2, 0x00);
    /* 0x20: user code    (present, ring3, code, exec/read) + long mode       */
    set_entry(4, 0xFA, 0x20);
    /* 0x28/0x30: TSS — filled in later by tss_init via gdt_set_tss. */
    gdt[5] = (struct gdt_entry){0};
    gdt[6] = (struct gdt_entry){0};

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint64_t)(uintptr_t)&gdt;

    x86_lgdt(&gdtr);
    gdt_reload_segments();
}

void gdt_set_tss(uint64_t base, uint32_t limit) {
    struct tss_descriptor *d = (struct tss_descriptor *)&gdt[5];
    d->limit_low  = (uint16_t)(limit & 0xFFFF);
    d->base_low   = (uint16_t)(base & 0xFFFF);
    d->base_mid   = (uint8_t)((base >> 16) & 0xFF);
    d->access     = 0x89;                       /* present, type=0x9 (64-bit TSS available) */
    d->granularity = (uint8_t)((limit >> 16) & 0x0F);  /* G=0, byte granular */
    d->base_high  = (uint8_t)((base >> 24) & 0xFF);
    d->base_upper = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    d->reserved   = 0;
}
