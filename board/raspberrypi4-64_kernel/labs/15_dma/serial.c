// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/minmax.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

/* Add your code here */

#define SERIAL_RESET_COUNTER  0
#define SERIAL_GET_COUNTER    1
#define SERIAL_RX_LENGTH     16
#define SERIAL_TX_LENGTH      16

#define UARTRD    0x00  // read/write data
#define UARTRSR   0x04  // error clear/receive status
#define UARTFR    0x18  // flag register
#define UARTIBDR  0x24  // baud divisor rate, integer part
#define UARTFBDR  0x28  // baud divisor rate, fraction part
#define UARTLCR_H 0x2c  // line control register
#define UARTCR    0x30  // control register
#define UARTIFLS  0x34  // interrupt FIFO level select register
#define UARTIMSC  0x38  // interrupt mask set/clear
#define UARTMIS   0x40  // masked interrupt status register
#define UARTDMACR 0x48  // DMA controll register

#define WLEN_8BIT ((1 << 5) | (1 << 6)) // set length to 8 bit
#define BAUD_RATE 115200                // arbitrary baud rate
#define UARTEN    (1<<0)                // enable uart
#define LBE       (1<<7)                // enable loopback
#define TXE       (1<<8)                // enable transmit
#define RXE       (1<<9)                // enable receive
#define BUSY      (1<<3)                // busy status
#define RXIM      (1<<4)                // receive interrupt mask
#define RXMIS     (1<<4)                // receive masked interrupt status
#define TXDMAE    (1<<1)                // enable TX DMA
#define FEN       (1<<4)                // enable FIFO
#define RXFE      (1<<4)                // receive FIFO is empty
#define RTIM      (1<<6)                // receive timeout interrupt mask
#define RTMIS     (1<<6)                // receive timeout masked interrupt status

struct serial_dev {
	void __iomem *regs;
  struct platform_device *pdev;
  unsigned long uartclk;
  atomic64_t counter;

  struct miscdevice miscdev;

  struct mutex uart_lock;
  spinlock_t rx_lock;
  wait_queue_head_t rx_queue;
  volatile unsigned int rx_buf[SERIAL_RX_LENGTH];
  int rx_idx_rd;
  int rx_idx_wr;
  int rx_size;

  struct resource *iores;
  struct dma_chan *txchan;
  char txongoing;
  struct completion txcomplete;
  unsigned int tx_buf[SERIAL_TX_LENGTH*2];
};

static struct file_operations serial_fops_pio;
static struct file_operations serial_fops_dma;

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

  writel(0x00, ser->regs + UARTDMACR);
  writel(0x00, ser->regs + UARTIMSC);
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
  writel(WLEN_8BIT | FEN, ser->regs + UARTLCR_H);
  writel(UARTEN | TXE | RXE, ser->regs + UARTCR);

  return 0;
}

static int _write_char(struct serial_dev *ser, char c)
{
  int ret = 0;
  unsigned long flags = 0;
  struct device *dev = &ser->pdev->dev;

  if ((ret = mutex_lock_interruptible(&ser->uart_lock))) {
    return ret;
  }

  spin_lock_irqsave(&ser->rx_lock, flags);
  writel(c, ser->regs + UARTRD);
  spin_unlock_irqrestore(&ser->rx_lock, flags);
  ret = readl_poll_timeout(ser->regs + UARTFR, flags, !(flags & BUSY), 10, 100000);
  if (ret) {
    dev_err(dev, "Failed to write char %x, err %d\n", c, ret);
    ret = -EIO;
    goto write_failed;
  }

  if (c == '\n') {
    spin_lock_irqsave(&ser->rx_lock, flags);
    writel('\r', ser->regs + UARTRD);
    spin_unlock_irqrestore(&ser->rx_lock, flags);
    ret = readl_poll_timeout(ser->regs + UARTFR, flags, !(flags & BUSY), 10, 100000);
    if (ret) {
      dev_err(dev, "Failed to write char %x, err %d\n", c, ret);
      ret = -EIO;
      goto write_failed;
    }
  }
  atomic64_inc(&ser->counter);
  mutex_unlock(&ser->uart_lock);

  return 0;
write_failed:
  mutex_unlock(&ser->uart_lock);
  return ret;
}

static irqreturn_t serial_read_irq(int irq, void *priv)
{
  struct serial_dev *ser = (struct serial_dev *)(priv);

  // TODO currently we override the last char silently, fix it
  int masked_irq_status = readl(ser->regs + UARTMIS);
  if ((RXMIS | RTMIS) & masked_irq_status) {
    spin_lock(&ser->rx_lock);
    // drain the FIFO
    // dev_info(&ser->pdev->dev, "IRQ");
    while (!(readl(ser->regs + UARTFR) & RXFE)) {
      unsigned int c = readl(ser->regs + UARTRD);
      ser->rx_buf[ser->rx_idx_wr] = c;
      ser->rx_idx_wr = (ser->rx_idx_wr + 1) % SERIAL_RX_LENGTH;
      ser->rx_size = min(SERIAL_RX_LENGTH, ser->rx_size+1);
    }
    spin_unlock(&ser->rx_lock);
    wake_up(&ser->rx_queue);
    return IRQ_HANDLED;
  }
  return IRQ_NONE;
}

static int is_rx_buffer_empty(struct serial_dev *serial) {
  unsigned long flags = 0;
  spin_lock_irqsave(&serial->rx_lock, flags);
  int result = serial->rx_idx_rd == serial->rx_idx_wr && serial->rx_size == 0;
  spin_unlock_irqrestore(&serial->rx_lock, flags);
  return result;
}

static ssize_t serial_read(struct file *file, char __user *buffer,
			 size_t count, loff_t *ppos)
{
  if (!count){
    return 0;
  }

  struct miscdevice *miscdevice_ptr = file->private_data;
  struct serial_dev *serial = container_of(miscdevice_ptr, struct serial_dev, miscdev);

  // TODO signal full buffer
  int ret;
  unsigned long flags = 0;
  char local_buf[SERIAL_RX_LENGTH] = {0};
  
  // TODO add a function to lock while checking the condition. Linux does not lock it for us.
  if ((ret = wait_event_interruptible(serial->rx_queue, !is_rx_buffer_empty(serial)))) {
    return ret;
  }
  
  // TODO perform this operation in a loop. A second caller might end up with an empty buffer and return 0
  // Userspace interprets 0 as EOF and exists.
  spin_lock_irqsave(&serial->rx_lock, flags);
  int i = 0;
  for (; i < count && i < sizeof(local_buf) && !(serial->rx_idx_rd == serial->rx_idx_wr && serial->rx_size == 0); ++i)
  {
    local_buf[i] = serial->rx_buf[serial->rx_idx_rd];
    serial->rx_idx_rd = (serial->rx_idx_rd + 1) % SERIAL_RX_LENGTH;
    serial->rx_size--;
  }
  spin_unlock_irqrestore(&serial->rx_lock, flags);

  if ((ret = copy_to_user(buffer, local_buf, i))) {
    return -EINVAL;
  }

  *ppos += i;
  return i;
}

static ssize_t serial_write_pio(struct file *file, const char __user *buf,
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
    }

    *ppos += n;
    cur += n;
  }

  return count;
}

static void serial_write_dma_callback(void *ser)
{
  struct serial_dev *serial = (struct serial_dev *)ser;
  complete(&serial->txcomplete);
}

static ssize_t serial_write_dma(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
  unsigned long flags;
  int ret = 0;
  struct miscdevice *miscdevice_ptr = file->private_data;
  struct serial_dev *serial = container_of(miscdevice_ptr, struct serial_dev, miscdev);
  struct device *dev = &serial->pdev->dev;
  struct device *dma_dev = dmaengine_get_dma_device(serial->txchan);

  // one write operation at a time
  // TODO chain operations
  spin_lock_irqsave(&serial->rx_lock, flags);
  if (serial->txongoing) {
    spin_unlock_irqrestore(&serial->rx_lock, flags);
    return -EBUSY;
  }
  serial->txongoing = 1;
  spin_unlock_irqrestore(&serial->rx_lock, flags);

  // copy the data from the user
  int nchars = min(count, SERIAL_TX_LENGTH);
  char user_chars[SERIAL_TX_LENGTH];
  if ((ret = copy_from_user(user_chars, buf, nchars))) {
    ret = -EINVAL;
    goto unlock_write;
  }
  
  // copy user characters to 4 byte buffer - Broadcom only supports 4 byte wide transfers
  // copy \n as \n\r
  int dma_i = 0;
  for (int i=0; i<nchars && dma_i < sizeof(serial->tx_buf)/sizeof(serial->tx_buf[0]); ++i) {
    serial->tx_buf[dma_i++] = user_chars[i];
    if (user_chars[i] == '\n') {
      serial->tx_buf[dma_i++] = '\r';
    }
  }
  const int dma_len = dma_i * sizeof(serial->tx_buf[0]);

  // map the buffer for DMA controller
  dma_addr_t bounce_addr = dma_map_single(dma_dev, serial->tx_buf, dma_len, DMA_TO_DEVICE);
  if (dma_mapping_error(dma_dev, bounce_addr)) {
    dev_err(dev, "Error mapping bounce buffer");
    ret = -EIO;
    goto unlock_write;
  }

  struct dma_async_tx_descriptor *desc;
  desc = dmaengine_prep_slave_single(serial->txchan, bounce_addr, dma_len, DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT);
  if (!desc) {
    dev_err(dev, "Error prepping slave single: %d", ret);
    ret = -EIO;
    goto cleanup_write_dma;
  }

  desc->callback = serial_write_dma_callback;
  desc->callback_param = serial;
  reinit_completion(&serial->txcomplete);

  dma_cookie_t cookie;
  cookie = dmaengine_submit(desc);
  ret = dma_submit_error(cookie);
  if (ret) {
    dev_err(dev, "Error setting cookie: %d", ret);
    ret = -EINVAL;
    goto cleanup_write_dma;
  }
  dma_async_issue_pending(serial->txchan);
  ret = wait_for_completion_timeout(&serial->txcomplete, msecs_to_jiffies(2000));
  if (!ret) {
    // DEBUGGING DMA TIMEOUT!!!!!
    struct dma_tx_state state = {};
    enum dma_status status;
    status = dmaengine_tx_status(serial->txchan, cookie, &state);
    dev_info(dev, "DMA status=%d remaining=%u total=%zu\n", status, state.residue, (size_t)dma_len);
    dev_info(dev, "CR=%#x DMACR=%#x LCRH=%#x FR=%#x\n",
      readl(serial->regs + UARTCR),
      readl(serial->regs + UARTDMACR),
      readl(serial->regs + UARTLCR_H),
      readl(serial->regs + UARTFR));

    ret = -ETIMEDOUT;
		goto cancel_dma;
  }

  // cleanup dma mapping
  dma_unmap_single(dma_dev, bounce_addr, dma_len, DMA_TO_DEVICE);
  // unlock the write
  spin_lock_irqsave(&serial->rx_lock, flags);
  serial->txongoing = 0;
  spin_unlock_irqrestore(&serial->rx_lock, flags);
  // increment the counter
  atomic64_add(nchars, &serial->counter);
  *ppos += nchars;
  return nchars;

cancel_dma:
	dmaengine_terminate_sync(serial->txchan);
cleanup_write_dma:
  dma_unmap_single(dma_dev, bounce_addr, dma_len, DMA_TO_DEVICE);
unlock_write:
  spin_lock_irqsave(&serial->rx_lock, flags);
  serial->txongoing = 0;
  spin_unlock_irqrestore(&serial->rx_lock, flags);

  return ret;
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
      atomic64_set(&serial->counter, 0);
      break;
    default:
      return -EINVAL;
  }
  return 0;
}

static int _serial_init_dma(struct serial_dev *ser)
{
  int ret = 0;
  struct device *dev = &ser->pdev->dev;
  init_completion(&ser->txcomplete);
  ser->txchan = dma_request_chan(dev, "tx");
  if (IS_ERR(ser->txchan)) {
    ret = PTR_ERR(ser->txchan);
    ser->txchan = NULL;
    dev_err(dev, "Error setting DMA: %d\n", ret);
    return ret;
  }

  struct dma_slave_config txconf = {};

  txconf.direction = DMA_MEM_TO_DEV;
  txconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
  txconf.dst_addr = ser->iores->start + UARTRD;
  dev_info(dev, "dst_addr: %pa\n", &txconf.dst_addr);

  ret = dmaengine_slave_config(ser->txchan, &txconf);
  if (ret) {
    dev_err(dev, "Error setting DMA config: %d", ret);
    goto cleanup_release_channel;
  }

  // enable DMA on the UART side
  writel(TXDMAE, ser->regs + UARTDMACR);

  return 0;

cleanup_release_channel:
  dma_release_channel(ser->txchan);
  ser->txchan = NULL;
  return ret;
}
static void _serial_cleanup_dma(struct serial_dev *ser)
{
  if (!ser->txchan) {
    return;
  }
  dmaengine_terminate_sync(ser->txchan);
  dma_release_channel(ser->txchan);
}

static int serial_probe(struct platform_device *pdev)
{
  pr_info("Called %s\n", __func__);
  struct device *dev = &pdev->dev;
  struct serial_dev *serial = NULL;
  int ret = 0;
  int irq = 0;

  // create our driver struct
	serial = devm_kzalloc(dev, sizeof(*serial), GFP_KERNEL);
  if (!serial) {
    return -ENOMEM;
  }
  atomic64_set(&serial->counter, 0);
  spin_lock_init(&serial->rx_lock);
  mutex_init(&serial->uart_lock);
  init_waitqueue_head(&serial->rx_queue);
  serial->pdev = pdev;

  // create mapping from virt address to physical bus address
  serial->regs = devm_platform_ioremap_resource(pdev, 0);
  if (IS_ERR(serial->regs)) {
    return PTR_ERR(serial->regs);
  }
  // get the MMIO device tree description
  serial->iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
  if (IS_ERR(serial->iores)) {
    return PTR_ERR(serial->iores);
  }
  // enable the clocks
  if ((ret = _serial_enable_clocks(serial))) {
    goto init_cleanup;
  }

  // setup the UART for comms
  if ((ret = _setup_uart(serial))) {
    goto init_cleanup;
  }

  // enable the interrupt
  irq = platform_get_irq(pdev, 0);
  if (irq < 0) {
    ret = dev_err_probe(dev, irq, "Failed to get the interrupt\n");
    goto init_cleanup;
  }
  if ((ret = devm_request_irq(dev, irq, serial_read_irq, IRQF_SHARED, dev_name(dev), serial))) {
    ret = dev_err_probe(dev, ret, "Failed to register interrupt\n");
    goto init_cleanup;
  }
  writel(0x00, serial->regs + UARTIFLS);
  writel(RXIM | RTIM, serial->regs + UARTIMSC);

  ret = _serial_init_dma(serial);

  // register misc
  serial->miscdev.name = devm_kasprintf(dev, GFP_KERNEL, "serial-%llx", serial->iores->start);
  serial->miscdev.minor = MISC_DYNAMIC_MINOR;
  serial->miscdev.fops = ret == 0 ? &serial_fops_dma : &serial_fops_pio;
  serial->miscdev.parent = dev;
  if ((ret = misc_register(&serial->miscdev))) {
    ret = dev_err_probe(dev, ret, "Failed to register misc\n");
    goto dma_cleanup;
  }

  platform_set_drvdata(pdev, serial);
  return 0;

  // error cleanup
dma_cleanup:
  _serial_cleanup_dma(serial);
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
  _serial_cleanup_dma(serial);
  _cleanup_uart(serial);
  // TODO disable clocks? or are they shared?
}

static struct file_operations serial_fops_pio = {
  .owner = THIS_MODULE,
  .write = serial_write_pio,
  .read = serial_read,
  .unlocked_ioctl = serial_ioctl,
};
static struct file_operations serial_fops_dma = {
  .owner = THIS_MODULE,
  .write = serial_write_dma,
  .read = serial_read,
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
