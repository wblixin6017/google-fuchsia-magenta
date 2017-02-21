// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi/protocol/loaded-image.h>
#include <efi/protocol/simple-file-system.h>

#include <xefi.h>
#include <stdio.h>

efi_file_protocol* xefi_open_file(char16_t* filename, uint64_t mode) {
    efi_loaded_image_protocol* loaded;
    efi_status r;
    efi_file_protocol* file = NULL;

    r = xefi_open_protocol(gImg, &LoadedImageProtocol, (void**)&loaded);
    if (r) {
        printf("LoadFile: Cannot open LoadedImageProtocol (%s)\n", xefi_strerror(r));
        goto exit0;
    }

    efi_simple_file_system_protocol* sfs;
    r = xefi_open_protocol(loaded->DeviceHandle, &SimpleFileSystemProtocol, (void**)&sfs);
    if (r) {
        printf("LoadFile: Cannot open SimpleFileSystemProtocol (%s)\n", xefi_strerror(r));
        goto exit1;
    }

    efi_file_protocol* root;
    r = sfs->OpenVolume(sfs, &root);
    if (r) {
        printf("LoadFile: Cannot open root volume (%s)\n", xefi_strerror(r));
        goto exit2;
    }

    r = root->Open(root, &file, filename, mode, 0);
    if (r) {
        printf("LoadFile: Cannot open file (%s) with mode 0x%016lx\n", xefi_strerror(r), mode);
        goto exit3;
    }

exit3:
    root->Close(root);
exit2:
    xefi_close_protocol(loaded->DeviceHandle, &SimpleFileSystemProtocol);
exit1:
    xefi_close_protocol(gImg, &LoadedImageProtocol);
exit0:
    return file;
}

void* xefi_read_file(efi_file_protocol* file, size_t* _sz) {
    efi_status r;
    size_t pages = 0;
    void* data = NULL;

    char buf[512];
    size_t sz = sizeof(buf);
    efi_file_info* finfo = (void*)buf;
    r = file->GetInfo(file, &FileInfoGuid, &sz, finfo);
    if (r) {
        printf("LoadFile: Cannot get FileInfo (%s)\n", xefi_strerror(r));
        return NULL;
    }

    pages = (finfo->FileSize + 4095) / 4096;
    r = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, (efi_physical_addr *)&data);
    if (r) {
        printf("LoadFile: Cannot allocate buffer (%s)\n", xefi_strerror(r));
        return NULL;
    }

    sz = finfo->FileSize;
    r = file->Read(file, &sz, data);
    if (r) {
        printf("LoadFile: Error reading file (%s)\n", xefi_strerror(r));
        gBS->FreePages((efi_physical_addr)data, pages);
        return NULL;
    }
    if (sz != finfo->FileSize) {
        printf("LoadFile: Short read\n");
        gBS->FreePages((efi_physical_addr)data, pages);
        return NULL;
    }
    *_sz = finfo->FileSize;

    return data;
}

efi_status xefi_write_file(efi_file_protocol* file, void* data, size_t* _sz) {
    if (file == NULL || data == NULL || _sz == 0) {
        return EFI_INVALID_PARAMETER;
    }

    return file->Write(file, _sz, data);
}


void* xefi_load_file(char16_t* filename, size_t* _sz) {
    efi_file_protocol* file = xefi_open_file(filename, EFI_FILE_MODE_READ);
    if (!file) {
        return NULL;
    }
    void* data = xefi_read_file(file, _sz);
    file->Close(file);
    return data;
}

void xefi_close_file(efi_file_protocol* file) {
    if (!file) {
        return;
    }

    file->Close(file);
}

/*
 * Delete a file
 * Return values:
 *  -1:     couldn't open filename
 *  -2:     couldn't delete filename
 */
int xefi_unlink(char16_t* filename) {
    efi_file_protocol* file;

    file = xefi_open_file(filename, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE);
    if (!file) {
        return -1;
    }

    if (file->Delete(file) != EFI_SUCCESS) {
        return -2;
    }

    return 0;
}

/*
 * Move a file on the filesystem from src to dst
 * Return values:
 *   0:     Success
 *  -1:     Invalid parameters
 *  -2:     Could not open src for reading
 *  -3:     Could not open dst for creation/writing
 *  -4:     Could not write to dst
 *  -5:     Could not delete src
 */
int xefi_rename(char16_t* src, char16_t* dst) {
    void *data = NULL;
    size_t sz;
    efi_file_protocol* file = NULL;

    if (!src || !dst) {
        return -1;
    }

    data = xefi_load_file(src, &sz);
    if (!data) {
        return -2;
    }

    // Open dst and copy the buffer to it
    file = xefi_open_file(dst,
            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE);
    if (!file) {
        return -3;
    }

    if (xefi_write_file(file, data, &sz) != EFI_SUCCESS || sz == 0) {
        return -4;
    }

    // Close cannot fail
    xefi_close_file(file);

    // Now that the dst copy is finished, delete src
    if (xefi_unlink(src) < 0) {
        return -5;
    }

    return 0;
}

