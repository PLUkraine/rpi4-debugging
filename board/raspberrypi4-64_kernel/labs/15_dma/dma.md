# DMA

## Prepare the DMA controller

Raspberry PI 4 uses a different DMA controller, bcm2835-dma. It requires us to enabling UART FIFO hardware queue.
The FIFO protects UART from missing a byte when there is a burst of data and the driver is lagging behind.

To enable FIFO, you need to modify the `UARTLCR_H` register to set the `FEN` flag like this:

```C
#define FEN       (1<<4)                // enable FIFO
// in setup_uart
writel(WLEN_8BIT | FEN, ser->regs + UARTLCR_H);
```

Howver, enabling FIFO will break our read implementation. To fix it, we will need to enable the timeout interrupt and to check for both receive and timeout interrupts in the IRQ.
For now, you can ignore the read path and focus on the DMA TX portion. Please see `serial.c` to find the exact details of how to fix the read path.

Make sure to declare the `tx_buf` as a `u32` array. BCM2835 controller only works with 4 bytes of data. More on this later.

Our DMA controller does not need `fifo_dma_addr`. The iores resource already points to the physical address of the UART controller, and remapping will cause a kernel OOPS.

Additionally, BCM2835 DMA controller only supports 4 byte wide bus. Bootlin's suggested `DMA_SLAVE_BUSWIDTH_4_BYTES` value will cause an OOPS.

Sample code (do not forget to process every error!):

```C
struct dma_slave_config txconf = {};
txconf.direction = DMA_MEM_TO_DEV;
txconf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
txconf.dst_addr = ser->iores->start + UARTRD;
ret = dmaengine_slave_config(serial->txchan, &txconf);
if (ret) ...
```

## Prepare the UART controller

We need to set up a different  register. PL011 defines `UARTDMACR` at offset `0x48`.

```C
#define UARTDMACR 0x48  // DMA controll register
#define TXDMAE    (1<<1)                // enable TX DMA

// in setup_dma
writel(TXDMAE, ser->regs + UARTDMACR);
```

## Process user write requests

Our DMA controller does not require feeding the first character manually, so ignore that part.

Because our DMA bus is 4 bytes wide, we cannot directly copy bytes from user to `tx_buf`.
We need to bouce the data to intermediate `char[]` array, and then copy it to `tx_buf`.

Example:

```C

char user_chars[SERIAL_TX_LENGTH];
int nchars = min(count, SERIAL_TX_LENGTH);

if ((ret = copy_from_user(user_chars, buf, nchars))) {
    // cleaup DMA mapping and return -EINVAL
}

int dma_i = 0;
for (int i=0; i<nchars && dma_i < sizeof(serial->tx_buf)/sizeof(serial->tx_buf[0]); ++i) {
    serial->tx_buf[dma_i++] = user_chars[i];
}
const int dma_len = dma_i * sizeof(serial->tx_buf[0]);
```

Example of the DMA callback:

```C
static void serial_write_dma_callback(void *ser)
{
  struct serial_dev *serial = (struct serial_dev *)ser;
  complete(&serial->txcomplete);
}
```
