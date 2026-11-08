# Downloading Kernel Source Code

## Setup raspberrypi/linux

Raspberry Pi forked linux kernel, so we need to download their fork.

Download the source code next to your `buildroot` checkout. We will need buildroot's toolchain to compile the kernel.

```bash
git clone --branch rpi-6.18.y --depth 1 https://github.com/raspberrypi/linux.git
cd linux
```

There is no stable upstream to track, so ignore instruction to setup stable branch.

Apply the patch from our `rpi4-debugging` repo:

```bash
git apply ../rpi4-debugging/board/raspberrypi4-64_kernel/patches/linux/0001-disable-bt-overlay.patch
git add . && git commit -m "fix: enable uart"
export ARCH=arm64
export CROSS_COMPILE=../buildroot/output/host/bin/aarch64-linux-
make bcm2711_defconfig
```

Copy the kernel image and the new device tree to the TFTP root at `/var/lib/tfpt` or `/srv/tftp`.

```shell
TFTP_PATH=/srv/tftp
cp arch/arm64/boot/Image "$TFTP_PATH/ImageK"
cp arch/arm64/boot/dts/broadcom/bcm2711-rpi-4-b.dtb "$TFTP_PATH/bcm2711-rpi-4-b.dtb"
```

Update your `bootcmd` in U-Boot to point to the new `ImageK` and device tree:

```shell
setenv bootcmd 'setenv serverip 192.168.88.3; setenv ipaddr 192.168.88.50; tftpboot ${kernel_addr_r} ImageK; tftpboot ${fdt_addr_r} bcm2711-rpi-4-b.dtb; setenv bootargs console=ttyAMA0,115200 root=/dev/nfs rw nfsroot=192.168.88.3:/srv/nfs/rpi4-root/,nfsvers=3,tcp ip=dhcp::eth0:off; booti ${kernel_addr_r} - ${fdt_addr_r}'
```

## Next Steps

You will need to set up `ARCH` and `CROSS_COMPILE` variables every time you need to change the kernel.

We recommend you to create a `setup-linux.sh` script that you `source setup-linux.sh` on every new session.