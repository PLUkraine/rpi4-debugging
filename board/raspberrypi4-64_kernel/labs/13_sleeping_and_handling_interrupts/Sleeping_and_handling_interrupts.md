# Sleeping and handling interrupts

## Enable and filter the interrupts

The physical interrupt line is shared between all UART controllers. Your IRQ handler will be called whenever any UART interrupt is triggered.

We need to implement filtering in the IRQ routine.

```C
int masked_irq_status = readl(ser->regs + UARTMIS);
if (RXMIS & masked_irq_status) {
    // do stuff
}
```

We clear the interrupt by reading the UARTRD register.
