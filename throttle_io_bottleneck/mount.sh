#!/bin/bash
set -e

IMG_PATH="/root/loopdisk1.img"
MOUNT_POINT="/mnt/loop1"
SIZE="10G"

if [ ! -f "$IMG_PATH" ]; then
    echo "Creating $SIZE image at $IMG_PATH..."
    sudo fallocate -l $SIZE "$IMG_PATH"
fi

LOOP_DEV=$(sudo losetup -f)
sudo losetup -fP "$IMG_PATH"

if ! sudo blkid "$LOOP_DEV" >/dev/null 2>&1; then
    echo "Formatting $LOOP_DEV with ext4..."
    sudo mkfs.ext4 "$LOOP_DEV"
fi

sudo mkdir -p "$MOUNT_POINT"

sudo mount "$LOOP_DEV" "$MOUNT_POINT"
echo "Mounted $LOOP_DEV on $MOUNT_POINT"
