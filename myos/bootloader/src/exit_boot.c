/* Stages 12 + 15: final memory map capture and ExitBootServices. */
#include "bootloader.h"

/* The memory-map buffer is allocated once (before the final map) and reused on
 * retry, so no allocation happens between the final GetMemoryMap and exit. */
static EFI_MEMORY_DESCRIPTOR *g_map = NULL;
static UINTN g_map_capacity = 0;

EFI_STATUS bl_capture_memory_map(struct boot_info *bi, UINTN *out_map_key) {
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_ver = 0;

    /* First call (NULL buffer) reports the required size + descriptor size. */
    EFI_STATUS s = gBS->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_ver);
    if (s != EFI_BUFFER_TOO_SMALL) {
        return EFI_ERROR(s) ? s : EFI_LOAD_ERROR;
    }

    /* Allocate with generous slack: allocation itself can grow the map. */
    map_size += desc_size * 8;
    UINTN pages = (UINTN)EFI_SIZE_TO_PAGES(map_size);
    g_map = (EFI_MEMORY_DESCRIPTOR *)bs_alloc_pages(pages);
    if (!g_map) {
        return EFI_OUT_OF_RESOURCES;
    }
    g_map_capacity = pages * EFI_PAGE_SIZE;

    /* Final map: no allocations after this point. */
    UINTN cur = g_map_capacity;
    s = gBS->GetMemoryMap(&cur, g_map, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(s)) {
        return s;
    }

    bi->memory_map.map_base           = (myos_u64)(UINTN)g_map;
    bi->memory_map.map_size           = (myos_u64)cur;
    bi->memory_map.descriptor_size    = (myos_u64)desc_size;
    bi->memory_map.descriptor_version = (myos_u32)desc_ver;

    *out_map_key = map_key;
    return EFI_SUCCESS;
}

EFI_STATUS bl_exit_boot_services(struct boot_info *bi, UINTN map_key) {
    EFI_STATUS s = gBS->ExitBootServices(gImageHandle, map_key);
    if (!EFI_ERROR(s)) {
        return EFI_SUCCESS;
    }

    /* The map key went stale: refresh the map (no allocation) and retry once. */
    UINTN cur = g_map_capacity;
    UINTN desc_size = 0;
    UINT32 desc_ver = 0;
    UINTN new_key = 0;
    s = gBS->GetMemoryMap(&cur, g_map, &new_key, &desc_size, &desc_ver);
    if (EFI_ERROR(s)) {
        return s;
    }
    bi->memory_map.map_size           = (myos_u64)cur;
    bi->memory_map.descriptor_size    = (myos_u64)desc_size;
    bi->memory_map.descriptor_version = (myos_u32)desc_ver;

    return gBS->ExitBootServices(gImageHandle, new_key);
}
