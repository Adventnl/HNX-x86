/* Stage 12 support: UEFI memory allocation helpers + freestanding primitives. */
#include "bootloader.h"

void *bs_alloc_pages(UINTN pages) {
    EFI_PHYSICAL_ADDRESS addr = 0;
    EFI_STATUS s = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &addr);
    if (EFI_ERROR(s)) {
        return NULL;
    }
    return (void *)(UINTN)addr;
}

EFI_STATUS bs_alloc_pages_at(EFI_PHYSICAL_ADDRESS addr, UINTN pages) {
    EFI_PHYSICAL_ADDRESS a = addr;
    return gBS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &a);
}

void *bs_alloc_pool(UINTN size) {
    void *p = NULL;
    EFI_STATUS s = gBS->AllocatePool(EfiLoaderData, size, &p);
    if (EFI_ERROR(s)) {
        return NULL;
    }
    return p;
}

void bs_free_pool(void *p) {
    if (p) {
        gBS->FreePool(p);
    }
}

/* --- Freestanding memory routines (clang may also emit calls to these). --- */
void *memset(void *dst, int val, __SIZE_TYPE__ n) {
    unsigned char *d = (unsigned char *)dst;
    unsigned char v = (unsigned char)val;
    while (n--) {
        *d++ = v;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

int memcmp(const void *a, const void *b, __SIZE_TYPE__ n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    while (n--) {
        if (*pa != *pb) {
            return (int)*pa - (int)*pb;
        }
        pa++;
        pb++;
    }
    return 0;
}
