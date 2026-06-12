/* Stage 13: read framebuffer info from the UEFI Graphics Output Protocol. */
#include "bootloader.h"

static EFI_GUID g_gop = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

EFI_STATUS bl_get_framebuffer(struct boot_framebuffer *out) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS s = gBS->LocateProtocol(&g_gop, NULL, (void **)&gop);
    if (EFI_ERROR(s)) {
        return s;
    }
    if (!gop->Mode || !gop->Mode->Info) {
        return EFI_NOT_FOUND;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;

    out->base                = (myos_u64)gop->Mode->FrameBufferBase;
    out->size                = (myos_u64)gop->Mode->FrameBufferSize;
    out->width               = info->HorizontalResolution;
    out->height              = info->VerticalResolution;
    out->pixels_per_scanline = info->PixelsPerScanLine;
    out->bits_per_pixel      = 32;
    out->pitch               = info->PixelsPerScanLine * 4;
    out->pixel_format        = (myos_u32)info->PixelFormat;

    switch (info->PixelFormat) {
    case PixelBitMask:
        out->red_mask      = info->PixelInformation.RedMask;
        out->green_mask    = info->PixelInformation.GreenMask;
        out->blue_mask     = info->PixelInformation.BlueMask;
        out->reserved_mask = info->PixelInformation.ReservedMask;
        break;
    case PixelBlueGreenRedReserved8BitPerColor:
        out->blue_mask     = 0x000000FF;
        out->green_mask    = 0x0000FF00;
        out->red_mask      = 0x00FF0000;
        out->reserved_mask = 0xFF000000;
        break;
    case PixelRedGreenBlueReserved8BitPerColor:
    default:
        out->red_mask      = 0x000000FF;
        out->green_mask    = 0x0000FF00;
        out->blue_mask     = 0x00FF0000;
        out->reserved_mask = 0xFF000000;
        break;
    }
    return EFI_SUCCESS;
}
