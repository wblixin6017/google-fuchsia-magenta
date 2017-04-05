# mx_vmo_clone

## NAME

vmo_clone - create a VM object

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_vmo_clone(mx_handle_t handle, uint32_t options, uint64_t offset, uint64_t size, mx_handle_t* out);

```

## DESCRIPTION

**vmo_clone**() creates a new virtual memory object (VMO) that clones a range
of an existing vmo.

One handle is returned on success, representing an object with the requested
size.

*options* must currently contain only *MX_CLONE_COPY_ON_WRITE*.

If the COPY_ON_WRITE flag is set, the cloned vmo will behave the same way the
parent does, except that any write operation on the clone will bring in a copy
of the page at the offset the write occurred. That page in the cloned vmo is
now a copy and may diverge from the parent.

*offset* must be page aligned.

*offset* + *size* may not exceed the range of a 64bit unsigned value.

The following rights will be set on the handle by default:

**MX_RIGHT_READ** - May be read from or mapped with read permissions.

**MX_RIGHT_DUPLICATE** - The handle may be duplicated.

If the original handle has this right set it will be set on the clone:

**MX_RIGHT_TRANSFER** - The handle may be transferred to another process.

If *options* is *MX_CLONE_COPY_ON_WRITE* the following rights are added:

**MX_RIGHT_WRITE** - May be written to or mapped with write permissions.

*TEMPORARY* The following rights are added, will be removed:
**MX_RIGHT_EXECUTE** - May be mapped with execute permissions.

**MX_RIGHT_MAP** - May be mapped.

The *options* field is currently unused and must be set to 0.

## RETURN VALUE

**vmo_clone**() returns **NO_ERROR** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ERR_BAD_TYPE**  Input handle is not a VMO.

**ERR_ACCESS_DENIED**  Input handle does not have sufficient rights.

**ERR_INVALID_ARGS**  *out* is an invalid pointer or NULL, *options* is
any value other than 0, or the offset is not page aligned.

**ERR_OUT_OF_RANGE**  *offset* + *size* is too large.

**ERR_NO_MEMORY**  Failure due to lack of memory.

## SEE ALSO

[vmo_create](vmo_create.md),
[vmo_read](vmo_read.md),
[vmo_write](vmo_write.md),
[vmo_set_size](vmo_set_size.md),
[vmo_get_size](vmo_get_size.md),
[vmo_op_range](vmo_op_range.md),
[vmar_map](vmar_map.md).
