# Configuring pin muxing

## Difference from the BBB

On the BBB `i2c1` is useless until you add pin muxing. On the Raspberry Pi the
downstream `bcm2711-rpi-4-b.dts` already wires `&i2c1` to an `i2c1_pins` node,
so the bus works as soon as it is enabled. To get the lab's learning value,
first remove that muxing, see the bus fail, then write it back yourself.

## Find pin muxing information for i2c1

Open the BCM2711 ARM Peripherals document, chapter "GPIO":

- "Alternative Function Assignments" table: GPIO2 is `SDA1` and GPIO3 is `SCL1`
  in **ALT0**.
- `GPFSEL0` at bus address `0x7e200000` selects the function, 3 bits per pin.
  GPIO2 is bits 8:6, GPIO3 is bits 11:9. ALT0 is `0b100`.
- `GPIO_PUP_PDN_CNTRL_REG0` at offset `0xe4` sets pulls, 2 bits per pin.
  `0b01` is pull-up.

ARM physical addresses are bus address minus `0x7e000000` plus `0xfe000000`,
so `GPFSEL0` is at `0xfe200000`. Read it from the board:

```sh
# devmem 0xfe200000
```

Bits 11:6 read `100100` (`0x900`) when both pins are in ALT0.

The pinctrl debugfs files show the same thing in readable form:

```sh
# mount -t debugfs none /sys/kernel/debug
# grep -E "pin [23] " /sys/kernel/debug/pinctrl/fe200000.gpio-pinctrl-bcm2711/pinmux-pins
```

## Break the bus first

Drop the existing muxing in your custom DTS and reboot:

```dts
&i2c1 {
	/delete-property/ pinctrl-names;
	/delete-property/ pinctrl-0;
	status = "okay";
	clock-frequency = <100000>;
};
```

`devmem 0xfe200000` now shows GPIO2 and GPIO3 as inputs, and with the Nunchuk
wired `i2cdetect -r 1` finds nothing.

## Multiplex the I2C controller outputs

Look at how `bcm2711-rpi-ds.dtsi` describes `i2c1_pins`. The Broadcom pin
controller does not use offsets and mode numbers like the AM335x. It takes
GPIO numbers and a function:

- `brcm,pins`: GPIO numbers.
- `brcm,function`: `BCM2835_FSEL_ALT0` from `include/dt-bindings/pinctrl/bcm2835.h`.
- `brcm,pull`: `BCM2835_PUD_UP`. The board already has 1.8 kOhm pull-ups on
  GPIO2 and GPIO3, so the internal pull-up is optional here.

Write your own group under `&gpio` and point `&i2c1` at it:

```dts
&gpio {
	nunchuk_i2c1_pins: nunchuk-i2c1-pins {
		brcm,pins = <2 3>;
		brcm,function = <BCM2835_FSEL_ALT0>;
		brcm,pull = <BCM2835_PUD_UP>;
	};
};

&i2c1 {
	pinctrl-names = "default";
	pinctrl-0 = <&nunchuk_i2c1_pins>;
	status = "okay";
	clock-frequency = <100000>;
};
```

Rebuild the DTB, reboot, and check `devmem 0xfe200000` again.

## Wiring the I2C device

The Olimex MOD-Wii-UEXT-NUNCHUCK adapter has a UEXT connector. Wire it to the
40-pin header:

| UEXT pin | Signal | Raspberry Pi header pin |
|----------|--------|-------------------------|
| 1        | 3.3V   | 1 (3V3)                 |
| 2        | GND    | 9 (GND)                 |
| 5        | SCL    | 5 (GPIO3)               |
| 6        | SDA    | 3 (GPIO2)               |

All four wires land on the odd row of the Raspberry Pi header, the row away
from the board edge. Nothing goes on the even row, which carries 5 V on pins 2
and 4 and the serial console on pins 6, 8 and 10. The SoC pins are not 5 V
tolerant.

On the UEXT socket, pins 1 and 2 share the column next to the small triangle.
SCL and SDA are two columns further. Wire with the board unpowered.

```sh
# i2cdetect -y -r 1
```

The Nunchuk answers at `0x52`.
