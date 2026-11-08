# Accessing I/O memory and ports

## Add UART devices

We are using UART2 on the Raspberry Pi 4.

According to the board [pinout](https://pinout.xyz/) we need to connect UART TX to pin 28 (GPIO 1), UART RX to pin 27 (GPIO 0), and ground to pin 34.

Modify `arch/arm64/boot/dts/broadcom/bcm2711-rpi-4-b-custom.dts`

```dts
// SPDX-License-Identifier: GPL-2.0
#include "arm/broadcom/bcm2711-rpi-4-b.dts"

&uart2 {
    status = "okay";
    compatible = "custom,uart";
};

&uart3 {
    status = "okay";
    compatible = "custom,uart";
};
```

Compile and update your DTB.

## Device initialization

### Accessing device registers

RPI4 has a different UART controller than BBB. RPI4 uses PL011. We do not need to define `reg_read` function since we can just use `readl` and `writel` directly.

Define these magic constants in your code

```C
#define UARTRD    0x00  // read/write data
#define UARTRSR   0x04  // error clear/receive status
#define UARTFR    0x18  // flag register
#define UARTIBDR  0x24  // baud divisor rate, integer part
#define UARTFBDR  0x28  // baud divisor rate, fraction part
#define UARTLCR_H 0x2c  // line control register
#define UARTCR    0x30  // control register

#define WLEN_8BIT ((1 << 5) | (1 << 6)) // set length to 8 bit
#define BAUD_RATE 115200                // arbitrary baud rate
#define UARTEN    (1<<0)                // enable uart
#define LBE       (1<<7)                // enable loopback
#define TXE       (1<<8)                // enable transmit
#define RXE       (1<<9)                // enable receive
#define BUSY      (1<<3)                // busy status
```

### Power management initialization

We do not have power management ready for our controller. Instead we will enable the clock directly in the driver

```C
static int _serial_enable_clocks(struct serial_dev *ser)
{
  struct clk *clock_dev = NULL;
  struct device *dev = &(ser->pdev->dev);
  clock_dev = devm_clk_get_enabled(dev, "apb_pclk");
  if (IS_ERR(clock_dev)) {
    return dev_err_probe(dev, PTR_ERR(clock_dev), "cannot enable apb_pclk\n");
  }

  clock_dev = devm_clk_get_enabled(dev, "uartclk");
  if (IS_ERR(clock_dev)) {
    return dev_err_probe(dev, PTR_ERR(clock_dev), "cannot enable uartclk\n");
  }
  ser->uartclk = clk_get_rate(clock_dev);

  return 0;
}
```

Call `_serial_enable_clocks` in probe function.

### Line and baud rate configuration

PL011 has a different setup sequence than OMAP controller. According to the [documentation](https://www.taylortjohnson.com/class/cse2312/f14/uart_manual.pdf), page 3-16, we need to perform the following steps:

1. Disable the UART
2. Wait for the end of transmission or reception of the current character.
3. Flush the transmit FIFO by disabling bit 4 (FEN) in the line control register
(UARTCLR_H).
4. Reprogram the control register.
5. Enable the UART.

Baud rate programming requires setting 2 registers, UARTIBDR and UARTFBDR. It involves convoluted computation of the baud rate divisior that controlls the UART internal clocking.
Please read [documentation](https://www.taylortjohnson.com/class/cse2312/f14/uart_manual.pdf), page 3-10, to learn more about how the baud rate is calculated.

```C
static inline unsigned long compute_uartfbrd(unsigned long uartclk, unsigned long baud_rate) {
  unsigned long div = (16UL * baud_rate);
  unsigned long rem = uartclk % (16UL * baud_rate);

  return (128UL * rem + div) / (2UL * div);
}

static inline unsigned long compute_uartibrd(unsigned long uartclk, unsigned long baud_rate) {
  return uartclk / (16UL * baud_rate);
}

static int _cleanup_uart(struct serial_dev *ser)
{
  unsigned flags = 0;
  int ret = 0;

  writel(0x00, ser->regs + UARTCR);
  ret = readl_poll_timeout(ser->regs + UARTFR, flags, !(flags & BUSY), 10, 100000);
  WARN_ON(ret != 0);
  return 0;
}

static int _setup_uart(struct serial_dev *ser)
{
  _cleanup_uart(ser);

  // TODO fix fractional == 0 edge case
  writel(compute_uartibrd(ser->uartclk, BAUD_RATE), ser->regs + UARTIBDR);
  writel(compute_uartfbrd(ser->uartclk, BAUD_RATE), ser->regs + UARTFBDR);
  writel(WLEN_8BIT, ser->regs + UARTLCR_H);
  writel(UARTEN | LBE | TXE | RXE, ser->regs + UARTCR);

  return 0;
}
```

### FIFOs reset

We already did this in the `_setup_uart` when we cleaned up UART. No need to perform anything here.

### Standalone write routine

According to the documentation, we need to wait until device is not busy by polling UARTFR register, and then reading/writting to it.

```C

// write
writel('H', ser->regs + UARTRD);
ret = readl_poll_timeout(ser->regs + UARTFR, flags, !(flags & BUSY), 10, 100000);

// read
unsigned int data = readl(ser->regs + UARTRD);
char c = (char)(data & 0xFF);
```

Use these routines to read or write data on UART controller.
