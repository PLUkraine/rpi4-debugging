#!/bin/bash

################################################################################
# This is a script to build artifacts and transfer them over TFTP and NFS for
# the case when the build host and the TFT/NFS host are different hosts.
#
# Usage:
#    ```
#    make && make sync-2
#    ```
# 
# Configuration
# Set variable TFTP_NFS_SERVER_IP, which holds the TFTP and NFS host IP,
# in this script below.
# 
# TFTP configuration
# 1. Install atftp client on the build host because it allows to speed up
#    TFTP file transfer to the TFTP host, compared to the default tftp client,
#    by specifying the block size of transferred packet, see RFC2348.
#    ```
#    sudo apt-get install atftp
#    ```
# 2. Configure the block size of transferred packets and allow writes
#    on the tftp server in /etc/default/tftpd-hpa:
#    ```
#    # /etc/default/tftpd-hpa
#
#    TFTP_USERNAME="tftp"
#    TFTP_DIRECTORY="/srv/tftp"
#    TFTP_ADDRESS="0.0.0.0:69"
#    TFTP_OPTIONS="--secure --permissive --verbose --create --blocksize 65464"
#    ```
# 3. Let the tftp user own to the `/srv/tftp` directory:
#    ```
#    sudo chown tftp:tftp -R /srv/tftp
#    ```
# 4. Restart tftpd-hpa service on the TFTP host to apply the changes
#    ```
#    service tftpd-hpa restart
#    service tftpd-hpa status
#    ```
#
# NFS configuration
# 1. Install nfs-common package on the build host
#    ```
#    sudo apt-get install nfs-common
#    ```
# 2. Allow insecure read and writes in /etc/exports on the NFS host.
#    Given you are working on a home private network, it should be acceptable.
#    ```
#    ...
#    /srv/nfs/rpi4-root *(rw,sync,no_subtree_check,no_root_squash,insecure)
#    ```
# 3. Restart nfs-kernel-server to apply the changes
#    ```
#    service nfs-kernel-server restart
#    service nfs-kernel-server status
#    ```
################################################################################


set -euo pipefail

# common variables
NFS_ROOT="/srv/nfs/rpi4-root"
TFTP_NFS_SERVER_IP="<TFTP_NFS_SERVER_IP>"
DTB_NAME="bcm2711-rpi-4-b.dtb"
ROOTFS_NAME="rootfs.ext2"
KERNEL_NAME="Image"
# distro-dependent
TFTP_DIR="TO_BE_SET"
NFS_SERVICE="TO_BE_SET"
IMAGES_DIR="TO_BE_SET"

parse_args() {
    if [ "$#" -ne 1 ]; then
        echo "Usage: $0 /path/to/buildroot/output/images" >&2
        exit 1
    fi
    IMAGES_DIR="$1"
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
    echo "  TFTP dir:   ${TFTP_NFS_SERVER_IP}"
    echo "  NFS root:   ${TFTP_NFS_SERVER_IP}"
    echo
}

update_tftp() {
    echo "==> Updating TFTP (${TFTP_NFS_SERVER_IP})"
    atftp ${TFTP_NFS_SERVER_IP}"" --verbose --put --local-file "${IMAGES_DIR}/${KERNEL_NAME}" --remote-file "${KERNEL_NAME}" --option "blksize 65464"
    atftp ${TFTP_NFS_SERVER_IP}"" --verbose --put --local-file "${IMAGES_DIR}/${DTB_NAME}" --remote-file "${DTB_NAME}" --option "blksize 65464"
}

cleanup_nfs() {
    sudo umount $1 2>/dev/null || true
    rm -rf $1 2>/dev/null || true
}

update_nfs() {
    NFS_REMOTE="${TFTP_NFS_SERVER_IP}:${NFS_ROOT}/"
    echo
    echo "==> Updating NFS root (${NFS_REMOTE})"

    MOUNT_POINT_LOCAL="$(mktemp -d)"
    MOUNT_POINT_REMOTE="$(mktemp -d)"

    sudo mount -t nfs "${NFS_REMOTE}" "${MOUNT_POINT_REMOTE}"
    sudo mount -o loop,ro "${IMAGES_DIR}/rootfs.ext2" "${MOUNT_POINT_LOCAL}"
    sudo rsync -a --verbose --delete --exclude=root/ "${MOUNT_POINT_LOCAL}/" "${MOUNT_POINT_REMOTE}"
    sudo rsync -a --verbose "./debugging-beagleplay-labs/nfsroot/root/" "${MOUNT_POINT_REMOTE}/root/"

    cleanup_nfs "${MOUNT_POINT_LOCAL}"
    cleanup_nfs "${MOUNT_POINT_REMOTE}"
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
