# missed_interrupt

See https://forums.raspberrypi.com/viewtopic.php?t=338900 for context and discussion.

This is a cleaned-up reproduction of the issue:

- start two DMA channels simultaneously
- receive both of their interrupt flags in the same call to the handler
- attempting to use hw_set_bits or hw_clear_bits to clear INTS1 incorrectly clears *both* flags

![scope trace](/DS1Z_QuickPrint4.png)

Events from top to bottom and left to right:
- Channel 4 (blue) toggled in the main loop just before starting the transfer.
- Channel 3 (cyan) toggled in the interrupt handler about 1us later, after the busy-wait, indicating that DMA channel 1's interrupt bit is set.
- Channel 2 (pink) toggled in the same interrupt handler, indicating that DMA channel 2's interrupt bit is also set.
- Channel 1 (yellow) goes high, indicating that some bit in INTS is set.
- Interrupt handler calls hw_set_bits(INTS, 0)
- Channel 1 goes low, showing that INTS is now 0.

Precisely the same behavior occurs with hw_clear_bits and hw_xor_bits.

