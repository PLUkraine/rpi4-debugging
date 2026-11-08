// SPDX-License-Identifier: GPL-2.0
#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/minmax.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/uaccess.h>

/* Add your code here */

#define SERIAL_RESET_COUNTER  0
#define SERIAL_GET_COUNTER    1

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
  unsigned long counter;

  struct miscdevice miscdev;
};

static struct file_operations serial_fops;

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
  writel(UARTEN | TXE | RXE, ser->regs + UARTCR);

  return 0;
}

static int _write_char(struct serial_dev *ser, char c)
{
  int ret = 0;
  int flags = 0;
  struct device *dev = &ser->pdev->dev;
  writel(c, ser->regs + UARTRD);
  ret = readl_poll_timeout(ser->regs + UARTFR, flags, !(flags & BUSY), 10, 100000);
  if (ret) {
    dev_err(dev, "Failed to write char %x", c);
    return -EIO;
  }

  if (c == '\n') {
    writel('\r', ser->regs + UARTRD);
    ret = readl_poll_timeout(ser->regs + UARTFR, flags, !(flags & BUSY), 10, 100000);
    if (ret) {
      dev_err(dev, "Failed to write char %x", c);
      return -EIO;
    }
  }

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
 
static ssize_t serial_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
  struct miscdevice *miscdevice_ptr = file->private_data;
  struct serial_dev *serial = container_of(miscdevice_ptr, struct serial_dev, miscdev);
  
  char buffer[512] = {0};
  int ret = 0;
  size_t cur = 0;
  while (cur < count)
  {
    size_t n = min(sizeof(buffer), count - cur);
    if ((ret = copy_from_user(buffer, buf+cur, n))) {
      return -EINVAL;
    }

    for (int i=0; i<n; ++i) {
      ret = _write_char(serial, buffer[i]);
      if (ret) {
        return ret;
      }
      serial->counter++;
    }

    cur += n;
  }

  return count;
}

static long serial_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
  struct miscdevice *miscdevice_ptr = file->private_data;
  struct serial_dev *serial = container_of(miscdevice_ptr, struct serial_dev, miscdev);
  void __user *argp = (void __user *)arg;

  switch (cmd) {
    case SERIAL_GET_COUNTER:
      if (copy_to_user(argp, &serial->counter, sizeof(serial->counter))) {
        return -EINVAL;
      }
      break;
    case SERIAL_RESET_COUNTER:
      serial->counter = 0;
      break;
    default:
      return -EINVAL;
  }
  return 0;
}

static int serial_probe(struct platform_device *pdev)
{
  pr_info("Called %s\n", __func__);
  struct resource *res;
  struct device *dev = &pdev->dev;
  struct serial_dev *serial = NULL;
  int ret = 0;

	serial = devm_kzalloc(dev, sizeof(*serial), GFP_KERNEL);
  if (!serial) {
    return -ENOMEM;
  }
  serial->pdev = pdev;
  serial->counter = 0;

  serial->regs = devm_platform_ioremap_resource(pdev, 0);
  if (IS_ERR(serial->regs)) {
    return PTR_ERR(serial->regs);
  }
  res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
  if (IS_ERR(serial->regs)) {
    return PTR_ERR(serial->regs);
  }
  if ((ret = _serial_enable_clocks(serial))) {
    goto init_cleanup;
  }

  if ((ret = _setup_uart(serial))) {
    goto init_cleanup;
  }

  // register misc
  serial->miscdev.name = devm_kasprintf(dev, GFP_KERNEL, "serial-%llx", res->start);
  serial->miscdev.minor = MISC_DYNAMIC_MINOR;
  serial->miscdev.fops = &serial_fops;
  serial->miscdev.parent = dev;
  if ((ret = misc_register(&serial->miscdev))) {
    return dev_err_probe(dev, ret, "Failed to register misc\n");
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

  misc_deregister(&serial->miscdev);
  _cleanup_uart(serial);
  // TODO disable clocks? or are they shared?
}

static struct file_operations serial_fops = {
  .owner = THIS_MODULE,
  .write = serial_write,
  .unlocked_ioctl = serial_ioctl,
};

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
