#!/bin/bash

set -euo pipefail

# common variables
NFS_ROOT="/srv/nfs/rpi4-root"
DTB_NAME="bcm2711-rpi-4-b-merged.dtb"
ROOTFS_NAME="rootfs.ext2"
KERNEL_NAME="Image"
# distro-dependent
TFTP_DIR="TO_BE_SET"
NFS_SERVICE="TO_BE_SET"
BUILDROOT_DIR="TO_BE_SET"
IMAGES_DIR="TO_BE_SET"

parse_args() {
    if [ "$#" -ne 1 ]; then
        echo "Usage: $0 /path/to/buildroot" >&2
        exit 1
    fi
    BUILDROOT_DIR="$1"
    IMAGES_DIR="${BUILDROOT_DIR}/output/images"
}

validate_image_built() {
    for f in "${KERNEL_NAME}" "${DTB_NAME}" "${ROOTFS_NAME}"; do
        if [ ! -e "${IMAGES_DIR}/${f}" ]; then
            echo "ERROR: ${IMAGES_DIR}/${f} not found. Did the build complete?" >&2
            exit 1
        fi
    done
}

distro_detection() {
    if [ -r /etc/os-release ]; then
        . /etc/os-release
    else
        echo "ERROR: /etc/os-release not found, cannot detect distro." >&2
        exit 1
    fi

    case "${ID}" in
    fedora)
        TFTP_DIR="/var/lib/tftpboot"
        NFS_SERVICE="nfs-server"
        ;;
    ubuntu|debian)
        TFTP_DIR="/srv/tftp"
        NFS_SERVICE="nfs-kernel-server"
        ;;
    *)
        echo "ERROR: Unsupported distro '${ID}'. Only fedora and ubuntu/debian are supported." >&2
        exit 1
        ;;
    esac

    echo "Detected distro: ${PRETTY_NAME:-$ID}"
    echo "  TFTP dir:   ${TFTP_DIR}"
    echo "  NFS root:   ${NFS_ROOT}"
    echo
}

update_tftp() {
    echo "==> Updating TFTP (${TFTP_DIR})"
    sudo mkdir -p "${TFTP_DIR}"
    sudo cp -v "${IMAGES_DIR}/${KERNEL_NAME}" "${TFTP_DIR}/"
    sudo cp -v "${IMAGES_DIR}/${DTB_NAME}" "${TFTP_DIR}/"
}

cleanup_nfs() {
    sudo umount "${MOUNT_POINT}" 2>/dev/null || true
    rmdir "${MOUNT_POINT}" 2>/dev/null || true
}

update_nfs() {
    echo
    echo "==> Updating NFS root (${NFS_ROOT})"
    
    MOUNT_POINT="$(mktemp -d)"
    
    trap cleanup_nfs EXIT
    
    sudo mkdir -p "${NFS_ROOT}"
    sudo mount -o loop,ro "${IMAGES_DIR}/rootfs.ext2" "${MOUNT_POINT}"
    sudo rsync -a --delete --exclude=root/ "${MOUNT_POINT}/" "${NFS_ROOT}/"
    sudo umount "${MOUNT_POINT}"
    trap - EXIT
    rmdir "${MOUNT_POINT}"
    
    sudo exportfs -ra
}

main() {
    parse_args "$@"
    validate_image_built
    distro_detection
    update_tftp
    update_nfs

    echo
    echo "Done. Kernel, device tree, and rootfs are up to date."
}

main "$@"
