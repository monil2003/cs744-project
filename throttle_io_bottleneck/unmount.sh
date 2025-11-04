#!/bin/bash
set -e

MOUNT_POINT="/mnt/loop1"

LOOP_DEV=$(mount | grep "$MOUNT_POINT" | awk '{print $1}')

if [ -z "$LOOP_DEV" ]; then
    echo "No loop device mounted at $MOUNT_POINT"
    exit 0
fi

echo "Unmounting $LOOP_DEV from $MOUNT_POINT..."
sudo umount "$MOUNT_POINT"

echo "Detaching $LOOP_DEV..."
sudo losetup -d "$LOOP_DEV"

echo "Loop device $LOOP_DEV detached and unmounted."
