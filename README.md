# missed_interrupt

This is a minimal(?) reproduction of a weird issue I'm having with the Raspberry Pi Pico.

When using DMA to drive the SPI peripheral in full duplex, it appears to occasionally miss DMA-finished interrupts for the receiving channel. In my actual project, this happens reliably at a wide range of SPI frequencies; here, I've only been able to reproduce it at around 1MHz. It's clearly some kind of timing issue, right? Something between the DREQs and SPI FIFO? Or am I doing something wrong?

## pico forums post

Hello! I've been working on a fancy asynchronous DMA-driven SPI C++ library, but I'm having some trouble with the DMA-generated interrupts that are supposed to come at the end of every transfer block. It seems like the interrupt for the receiving channel sometimes completely fails to arrive.

I've put together a small reproduction of the issue here: https://github.com/NomiChirps/missed_interrupt/blob/main/missed_interrupt/missed_interrupt.cc

The setup is to claim 2 DMA channels; one reads from a dummy buffer and writes to the SPI data register (filling the TX FIFO), the other reads from the SPI data register and writes to a different dummy buffer (emptying the RX FIFO). Both are configured to use the appropriate SPI DREQ, and to generate an interrupt at the end of each transfer. I set an exclusive interrupt handler for DMA_IRQ_1 which simply tallies up the number of times each channel completes a transfer, putting the difference in a global variable. Finally, the main loop polls all 3 peripherals (DMA channel 0, DMA channel 1, and SPI) until they all report they're not busy, then triggers both DMA channels simultaneously.

In the long term the difference between the RX DMA interrupt count and the TX DMA interrupt count should stay at zero, because the SPI peripheral reads exactly one byte and puts it into the RX FIFO for every one byte written to the TX FIFO for transmission. Instead, I observe that the imbalance slowly grows over time, at a rate of about 10 per million transfers.

The timing is apparently pretty finicky; with this trimmed-down example code, I've only been able to reproduce the issue at SPI frequency 1MHz and transfer size of 1 byte. The way the interrupt handler checks for which channel's IRQ to acknowledge also seems to be important; if it checks both before acknowledging either, the imbalance grows much more slowly than if it does check0-ack0-check1-ack1. I don't know if that actually has something to do with the problem, or if it's just because this slightly changes the timing.

I'm pretty stumped at this point. Am I doing something subtly wrong in that code? I've configured everything correctly as far as I can tell. This kind of usage should be reliable, right?

## caught on the scope

![scope trace](/DS1Z_QuickPrint1.png)

From top to bottom:
- Channel 4 (blue) toggles in the main loop just before starting each transfer.
- Channel 3 (cyan) toggles in the interrupt handler each time the TX channel finishes.
- Channel 2 (pink) toggles in the interrupt handler each time the RX channel finishes.
- Channel 1 is the SPI SCLK signal.

Each transfer-start is followed by TX finishing after about 1us, then by RX finishing after about 10us.

The low-to-high transition of channel 4  just before the big gap in the middle is the problematic transfer being started.
Evidently the TX interrupt takes longer to trigger at that moment. The RX interrupt does not get triggered at all before the main loop polling sees that both channels are idle and starts the next transfer.
