# Using the I2C bus

## Setup

Copy the Bootlin `nunchuk/` lab directory into the NFS root, then point its
`Makefile` at our kernel tree. Set `ARCH` and `CROSS_COMPILE` first
(`source setup-linux.sh`, see the "Downloading Kernel" lab).

```sh
make KDIR=$HOME/path/to/linux
```

Copy `nunchuk.ko` to `/root/nunchuk/` on the board.

## Kernel configuration

`bcm2711_defconfig` builds the I2C controller driver and `/dev/i2c-*` as
modules. The modules in the NFS root belong to the Buildroot kernel, not to
yours, so they will not load. Build both into the kernel:

```sh
scripts/config -e I2C_BCM2835 -e I2C_CHARDEV
make olddefconfig
```

## Exploring /dev and /sys

Differences from the BBB:

- The console is `ttyAMA0`, the PL011 UART. There is no `ttyS0` because our
  patch disables the mini UART.
- The SD card is `mmcblk0`. There is no eMMC.
- The Ethernet interface is `eth0`, driven by `bcmgenet`.
- `/sys/class/thermal/thermal_zone0/temp` gives the SoC temperature.

## Driver tests

The Nunchuk device appears as `1-0052`:

```sh
# ls -l /sys/bus/i2c/drivers/nunchuk
# ls -l /sys/bus/i2c/devices/1-0052
```

The rest of the lab is board independent.
