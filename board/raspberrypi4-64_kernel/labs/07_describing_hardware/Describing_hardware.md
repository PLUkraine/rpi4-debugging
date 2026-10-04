# Describing Hardware Devices

## Create a custom device tree

Create `arch/arm64/boot/dts/broadcom/bcm2711-rpi-4-b-custom.dts`.

```dts
// SPDX-License-Identifier: GPL-2.0
#include "arm/broadcom/bcm2711-rpi-4-b.dts"
```

Modify `arch/arm64/boot/dts/broadcom/Makefile` to add your custom device tree.

## Driving LEDs

Raspberry Pi has one green LED that we can drive.
