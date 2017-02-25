
#include <efi/protocol/loaded-image.h>
#include <efi/protocol/simple-file-system.h>

#include <xefi.h>
#include <stdio.h>

/*
 * Backs up the old copy of gigaboot then writes the new version
 * Return values:
 *   0:     Success
 *  -1:     Invalid parameters
 *  -2:     Couldn't back up the bootloader
 *  -3:     Couldn't open new bootloader for writing
 *  -4:     Couldn't write the new bootloader
 */
int update_bootloader(void* bl_data, size_t sz) {
    char16_t* bl_path = L"bootx64.efi";
    char16_t* bl_bak_path = L"bootx64.efi.bak";
    efi_file_protocol* file = NULL;
    int ret;

    if (!bl_data || sz == 0) {
        return -1;
    }

    // Delete any backup unconditionally
    xefi_unlink(bl_bak_path);

    // Back up the existing bootloader
    ret = xefi_rename(bl_path, bl_bak_path);
    if (ret < 0) {
        printf("Failed to rename bootloader: %d\n", ret);
        return -2;
    }

    // Write the new bootloader
    file = xefi_open_file(bl_path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE);
    if (!file) {
        printf("Failed to open bootloader for writing\n");
        return -3;
    }

    if (xefi_write_file(file, bl_data, &sz) != EFI_SUCCESS) {
        printf("Failed to write bootloader data, %zu bytes written\n", sz);
        return -4;
    }
    xefi_close_file(file);

    return 0;
}
