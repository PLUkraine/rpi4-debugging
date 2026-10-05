# Training setup

## Install lab data

Bootlin provides full rootfs for their supported boards. For RPI4 we will have to compile our own.

Run the following commands to make our rootfs.

```sh

git clone https://github.com/PLUkraine/rpi4-debugging.git
git clone https://github.com/buildroot/buildroot.git
cd buildroot
git checkout 2026.02.x
make BR2_EXTERNAL=../rpi4-debugging raspberrypi4_64_kernel_defconfig
make
```

Then follow the README.md in the root of the repo to setup NFS and TFTP.

Make sure to also download Bootlin labs rootfs and copy only `/root` contents.

```sh
cd /tmp
wget https://bootlin.com/doc/training/linux-kernel/linux-kernel-bbb-labs.tar.xz
tar xvf linux-kernel-bbb-labs.tar.xz
rsync -a --info=progress2 linux-kernel-bbb-labs/modules/nfsroot/root /srv/nfs/rpi4-root
```
