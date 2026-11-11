# Sleeping and handling interrupts

## Enable and filter the interrupts

Our interrupt enable register `UARTIMSC` is at offset `0x38`. Set it right after `devm_request_irq`.

```C
#define UARTIMSC  0x38  // interrupt mask set/clear
#define RXIM      (1<<4)                // receive interrupt mask

writel(RXIM, serial->regs + UARTIMSC);
```

The physical interrupt line is shared between all UART controllers. Your IRQ handler will be called whenever any UART interrupt is triggered.

We need to implement filtering in the IRQ routine.

```C
#define RXMIS     (1<<4)                // receive masked interrupt status

int masked_irq_status = readl(ser->regs + UARTMIS);
if (RXMIS & masked_irq_status) {
    // do stuff
}
```

We clear the interrupt by reading the UARTRD register.
