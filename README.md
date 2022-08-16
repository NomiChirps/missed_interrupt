# missed_interrupt

This is a minimal(?) reproduction of a weird issue I'm having with the Raspberry Pi Pico.

When using DMA to drive the SPI peripheral in full duplex, it appears to occasionally miss DMA-finished interrupts for the receiving channel. In my actual project, this happens reliably at a wide range of SPI frequencies; here, I've only been able to reproduce it at around 1MHz. It's clearly some kind of timing issue, right? Something between the DREQs and SPI FIFO? Or am I doing something wrong?
