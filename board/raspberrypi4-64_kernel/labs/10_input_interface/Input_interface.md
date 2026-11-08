# Input interface

`CONFIG_INPUT_EVDEV` is already `y` in `bcm2711_defconfig`, skip that step.

The registration message shows the Raspberry Pi device path:

```
input: Wii Nunchuk as /devices/platform/soc/fe804000.i2c/i2c-1/1-0052/input/input0
```

`evtest` is in the root filesystem. The Nunchuk is usually `event0`, check the
list `evtest` prints.

The rest of the lab is board independent.
