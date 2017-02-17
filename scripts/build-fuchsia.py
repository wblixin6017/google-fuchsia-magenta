"""
Given a minfs container file, builds the fuchsia tree on the container.
"""

import subprocess
import os

# USAGE: fill in the following
MANIFEST_PATH = "</path/to/fuchsia>/out/release-aarch64/gen/packages/gn/user.bootfs.manifest" # Make sure you build a release aarch64 build.
IMAGE_PATH = "<path/to/minfs/image>/minfs-1g.img" # A 1gb (or larger) file formatted with minfs create
MINFS_TOOL_PATH = "</path/to/magenta>/build-rpi3-test/tools/minfs" # Pick a build directory that contains the minfs tools built

entries = []
existing_paths = set()

def ensure_path_exists(path):
    chunks = path.split("/")
    fullpath = ""
    res = []
    for chunk in chunks:
        fullpath += "/" + chunk
        if fullpath in existing_paths:
            continue

        existing_paths.add(fullpath)
        res.append(fullpath)
    return res

# Open the manifest file and read every line that maps a file on the local
# machine to a file on the disk image. These lines are in the following format:
# <PATH_ON_REMOTE_MACHINE>=<PATH_ON_LOCAL_MACHINE>
with open(MANIFEST_PATH, 'r') as fin:
    for line in fin:
        if "=" in line:
            # Convert to tuple and save.
            entries.append(line.strip().split("="))

allpaths = []

# First we need to ensure that the directory structure exists on the target
# device.
# We construct a list of all the directories on the remote device.
for entry in entries:
    dest = entry[0]
    src = entry[1]

    dest_dirname = os.path.dirname(dest)
    allpaths += ensure_path_exists(dest_dirname)
    
# Sort by length of the path so that we always create parents before their
# children.
allpaths = sorted(allpaths, key=lambda x: len(x))

# Create the directories on the target device.
for path in allpaths:
    subprocess.call([MINFS_TOOL_PATH, IMAGE_PATH, "mkdir", "::" + path])

# Copy the files from the host to the minfs image.
for entry in entries:
    dest = entry[0]
    src = entry[1]
    subprocess.call([MINFS_TOOL_PATH, IMAGE_PATH, "cp", src, "::/" + dest])

# Just a sanity step that prints the contents of the image.
for path in allpaths:
    print path
    subprocess.call([MINFS_TOOL_PATH, IMAGE_PATH, "ls", "::" + path])

