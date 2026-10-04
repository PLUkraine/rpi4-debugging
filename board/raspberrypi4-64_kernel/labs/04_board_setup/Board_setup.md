# Board setup

## Docs

Raspberry Pi 4 uses Broadcom 2711 SoC.

Docs:

- [BCM2711 ARM Peripherals](https://pip-assets.raspberrypi.com/categories/545-raspberry-pi-4-model-b/documents/RP-008248-DS-1-bcm2711-peripherals.pdf)
- [PrimeCell UART (PL011) TRM](https://support.arm.com/documentation/ddi0183/g)

## Modified Setup

We have already set up kernel sources for bcm2711 with `make bcm2711_defconfig`.

You do not have to add any other options, and can jump straight to building the kernel.

We have already set up netboot with NFS, so just drop new kernel Image or zImage and device tree to your TFTP folder and follow steps in the lab.
