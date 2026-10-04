# Kernel compiling and booting

## Cross-compiling toolchain setup

Since we have built the distro in buildroot, we can reuse the toolchain from it.

You can skip `sudo apt install gcc-arm-linux-gnueabi` and utilize our toolchain at `buildroot/output/host/bin/`.

## Kernel configuration

We have already set up kernel sources for bcm2711 with `make bcm2711_defconfig`.

You do not have to add any other options, and can jump straight to building the kernel.

We have already set up netboot with NFS, so just drop new kernel Image or zImage and device tree to your TFTP folder and follow steps in the lab.