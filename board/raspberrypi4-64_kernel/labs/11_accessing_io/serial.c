// SPDX-License-Identifier: GPL-2.0
#include <linux/clk.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>

/* Add your code here */

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

struct serial_dev {
	void __iomem *regs;
  struct platform_device *pdev;
  unsigned long uartclk;
};

static inline unsigned long compute_uartfbrd(unsigned long uartclk, unsigned long baud_rate) {
  unsigned long div = (16UL * baud_rate);
  unsigned long rem = uartclk % (16UL * baud_rate);

  return (128UL * rem + div) / (2UL * div);
}

static inline unsigned long compute_uartibrd(unsigned long uartclk, unsigned long baud_rate) {
  return uartclk / (16UL * baud_rate);
}

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

static int _cleanup_uart(struct serial_dev *ser)
{
  // struct device *dev = &ser->pdev->dev;
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

static int _loopback_test(struct serial_dev *ser)
{
  struct device *dev = &ser->pdev->dev;
  unsigned flags = 0;
  int ret = 0;

  writel('H', ser->regs + UARTRD);
  ret = readl_poll_timeout(ser->regs + UARTFR, flags, !(flags & BUSY), 10, 100000);

  dev_info(dev, "after: CR=%#x LCRH=%#x IBRD=%u FBRD=%u uartclk=%lu\n", 
    readl(ser->regs + UARTCR),
    readl(ser->regs + UARTLCR_H),
    readl(ser->regs + UARTIBDR),
    readl(ser->regs + UARTFBDR),
    ser->uartclk);
  if (ret) {
    return dev_err_probe(dev, ret, "UART stayed busy\n");
  }

  unsigned int data = readl(ser->regs + UARTRD);
  char c = (char)(data & 0xFF);
  if (c != 'H') {
    dev_err(dev, "Loopback failed. Expected %c, got %c. Hex value 0x%x\n", 'H', c, data);
    return -EIO;
  }
  return 0;
}

static int serial_probe(struct platform_device *pdev)
{
  pr_info("Called %s\n", __func__);
  struct device *dev = &pdev->dev;
  struct serial_dev *serial = NULL;
  int ret = 0;

	serial = devm_kzalloc(dev, sizeof(*serial), GFP_KERNEL);
  if (!serial) {
    return -ENOMEM;
  }
  serial->pdev = pdev;

  serial->regs = devm_platform_ioremap_resource(pdev, 0);
  if (IS_ERR(serial->regs)) {
    return PTR_ERR(serial->regs);
  }
  if ((ret = _serial_enable_clocks(serial))) {
    goto init_cleanup;
  }

  if ((ret = _setup_uart(serial))) {
    goto init_cleanup;
  }

  // try out loopback!
  if ((ret = _loopback_test(serial))) {
    goto init_cleanup;
  }

  platform_set_drvdata(pdev, serial);
  return 0;

init_cleanup:
  return ret;
}

static void serial_remove(struct platform_device *pdev)
{
  pr_info("Called %s\n", __func__);

  struct device *dev = &pdev->dev;
  struct serial_dev *serial = (struct serial_dev *)platform_get_drvdata(pdev);
  if (!serial) {
    dev_warn(dev, "No serial device to cleanup, %s", pdev->name);
    return;
  }

  _cleanup_uart(serial);
  // TODO disable clocks? or are they shared?
}


static const struct of_device_id serial_of_match[] = {
  { .compatible = "custom,uart" },
  { },
};
static struct platform_driver serial_driver = {
        .driver = {
                .name = "custom-uart",
                .owner = THIS_MODULE,
                .of_match_table = serial_of_match,
        },
        .probe = serial_probe,
        .remove = serial_remove,
};
module_platform_driver(serial_driver);

MODULE_DESCRIPTION( "Custom driver for training purposes");
MODULE_DEVICE_TABLE(of, serial_of_match);
MODULE_LICENSE("GPL");
