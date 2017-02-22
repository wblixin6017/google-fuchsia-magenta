// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

// put this in a special section, mark it read only
.section mdi,"a"
.balign 4096

// include the bootfs archive containing a user file system
DATA(embedded_mdi)
.incbin EMBEDDED_MDI_FILENAME
// The kernel needs this to contain whole pages. But mkbootfs, in the compressed
// case, does not pad the file size to a multiple of page size.
.balign 4096
END(embedded_mdi)

.Lembedded_mdi_end:

// save the length of the bootfs
DATA(embedded_mdi_len)
    .int .Lembedded_mdi_end - embedded_mdi
END(embedded_mdi_len)
