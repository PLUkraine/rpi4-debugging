# Describing Hardware Devices

## Create a custom device tree

Create `arch/arm64/boot/dts/broadcom/bcm2711-rpi-4-b-custom.dts`.

```dts
// SPDX-License-Identifier: GPL-2.0
#include "arm/broadcom/bcm2711-rpi-4-b.dts"
```

Modify `arch/arm64/boot/dts/broadcom/Makefile` to add your custom device tree.

## Setting the board's model name

Set `model` in your custom DTS as Bootlin describes, but do not expect to see it
on the board. Right before `booti`, U-Boot copies `/model` from the firmware's
device tree into ours (`ft_board_setup()` in `board/raspberrypi/rpi/rpi.c`), so
the kernel prints `Raspberry Pi 4 Model B Rev 1.x` anyway. Check that your
change is in the compiled DTB on the host instead:

```sh
fdtget arch/arm64/boot/dts/broadcom/bcm2711-rpi-4-b-custom.dtb / model
```

## Driving LEDs

Raspberry Pi has one green LED that we can drive.

## Managing I2C buses and devices

### Enabling an I2C bus

The 40-pin header exposes `i2c1` on GPIO2 (SDA, pin 3) and GPIO3 (SCL, pin 5).
Raspberry Pi OS calls it `i2c_arm`.

```sh
# i2cdetect -l
```

`i2c1` is missing because
`bcm270x-rpi.dtsi` sets it to `status = "disabled"`. On Raspberry Pi OS the
firmware flips it to `okay` when `config.txt` has `dtparam=i2c_arm=on`. We load
the DTB over TFTP from U-Boot, so firmware overrides never reach the kernel.
We enable the bus ourselves, like Bootlin does for the BBB.

Find the controller base address in the BCM2711 ARM Peripherals document,
chapter "BSC". `i2c1` is BSC1 at bus address `0x7e804000`. Linux names the
device after the ARM physical address, so expect `fe804000.i2c` in sysfs.
The device tree node is `i2c1: i2c@7e804000` in `bcm283x.dtsi`.

Enable the bus at 100 kHz in your custom DTS:

```dts
&i2c1 {
	status = "okay";
	clock-frequency = <100000>;
};
```

Check that the new bus shows up and points at the right controller:

```sh
# i2cdetect -l
# ls -l /sys/bus/i2c/devices/i2c-1
```

### Declare the Nunchuk device

Follow the Bootlin text. The node goes inside the `&i2c1` reference:

```dts
	joystick@52 {
		compatible = "nintendo,nunchuk";
		reg = <0x52>;
	};
```

The joystick node shows up at:

```sh
# find /sys/firmware/devicetree -name "*joystick*"
/sys/firmware/devicetree/base/soc/i2c@7e804000/joystick@52
```

`dt_binding_check` and `dtbs_check` work the same as on the BBB. Expect many
unrelated warnings from the downstream Raspberry Pi device trees.
