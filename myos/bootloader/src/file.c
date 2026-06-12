/* Stage 9: load a file from the UEFI System Partition (the boot volume). */
#include "bootloader.h"

static EFI_GUID g_loaded_image = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID g_simple_fs    = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID g_file_info    = EFI_FILE_INFO_ID;

EFI_STATUS bl_load_file(CHAR16 *path, void **out_buffer, UINTN *out_size) {
    EFI_STATUS s;
    EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;

    *out_buffer = NULL;
    *out_size = 0;

    /* Find the device this image was loaded from. */
    s = gBS->HandleProtocol(gImageHandle, &g_loaded_image, (void **)&li);
    if (EFI_ERROR(s)) return s;

    s = gBS->HandleProtocol(li->DeviceHandle, &g_simple_fs, (void **)&fs);
    if (EFI_ERROR(s)) return s;

    s = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(s)) return s;

    s = root->Open(root, &file, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(s)) {
        root->Close(root);
        return s;
    }

    /* Query the file size via GetInfo. */
    UINTN info_size = sizeof(EFI_FILE_INFO) + 256 * sizeof(CHAR16);
    EFI_FILE_INFO *info = (EFI_FILE_INFO *)bs_alloc_pool(info_size);
    if (!info) {
        file->Close(file);
        root->Close(root);
        return EFI_OUT_OF_RESOURCES;
    }
    s = file->GetInfo(file, &g_file_info, &info_size, info);
    if (EFI_ERROR(s)) {
        bs_free_pool(info);
        file->Close(file);
        root->Close(root);
        return s;
    }
    UINT64 file_size = info->FileSize;
    bs_free_pool(info);

    /* Allocate pages and read the whole file in. */
    UINTN pages = (UINTN)EFI_SIZE_TO_PAGES(file_size);
    void *buffer = bs_alloc_pages(pages);
    if (!buffer) {
        file->Close(file);
        root->Close(root);
        return EFI_OUT_OF_RESOURCES;
    }

    UINTN read_size = (UINTN)file_size;
    s = file->Read(file, &read_size, buffer);
    file->Close(file);
    root->Close(root);
    if (EFI_ERROR(s)) {
        return s;
    }

    *out_buffer = buffer;
    *out_size = read_size;
    return EFI_SUCCESS;
}
