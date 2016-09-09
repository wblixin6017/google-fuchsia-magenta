/*
test binding

bind with non-block device
bind with bad block size
bind with tiny device

all others
bind normally,release


IOCTL_BLOCK_GET_SIZE
IOCTL_BLOCK_GET_BLOCKSIZE
IOCTL_BLOCK_GET_GUID
IOCTL_BLOCK_GET_NAME
IOCTL_BLOCK_RR_PART
IOCTL_BLOCK_SET_VERITY_MODE
IOCTL_BLOCK_SET_VERITY_ROOT

read/write in enforce/bypass/ignore/enforce on small device

read in enforce on small device: pass
write in enforce on small device: fail
read in bypass on small device: pass
write in bypass on small device: pass
read in ignore on small device: pass
write in ignore on small device: fail
read in enforce on small device: fail
write in enforce on small device: fail

read lba=0 without allowing subsequent reads:fail

read lba=0 in enforce on large pseudo-device: pass
read lba=n/2 in enforce on large pseudo-device: pass
read lba=n-1 in enforce on large pseudo-device: pass

read lba=0 without allowing subsequent reads:pass
*/
