#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/time.h"

int c1_channel;
int c2_channel;

const int irq_index = 0;

const int debug_gpio_c1 = 6;
const int debug_gpio_c2 = 7;
const int debug_gpio_start = 8;
const int debug_gpio_ints = 9;

void __not_in_flash_func(DMA_ISR)() {
  // Wait long enough to ensure *both* channels have raised their interrupt.
  busy_wait_us(1);

  volatile uint32_t* ints = irq_index ? &dma_hw->ints1 : &dma_hw->ints0;
  if (*ints & (1<<c1_channel)) {
    gpio_put(debug_gpio_c1, !gpio_get(debug_gpio_c1));
  }
  if (*ints & (1<<c2_channel)) {
    gpio_put(debug_gpio_c2, !gpio_get(debug_gpio_c2));
  }

  gpio_put(debug_gpio_ints, *ints);

  // This evidently clears both channels, as does any other hw_*_bits()
  hw_clear_bits(ints, 0);
  // This would clear just the one channel, as advertised. (and put the interrupt handler into a loop because c2_channel is never cleared)
  //*ints = 1<<c1_channel;

  gpio_put(debug_gpio_ints, *ints);
}

int main() {
  busy_wait_ms(1000);

  gpio_init(debug_gpio_c1);
  gpio_init(debug_gpio_c2);
  gpio_init(debug_gpio_start);
  gpio_init(debug_gpio_ints);
  gpio_set_dir(debug_gpio_c1, GPIO_OUT);
  gpio_set_dir(debug_gpio_c2, GPIO_OUT);
  gpio_set_dir(debug_gpio_start, GPIO_OUT);
  gpio_set_dir(debug_gpio_ints, GPIO_OUT);

  const int transfer_count = 4;
  int c1_read;
  int c1_write;
  int c2_read;
  int c2_write;

  c1_channel = dma_claim_unused_channel(true);
  c2_channel = dma_claim_unused_channel(true);

  dma_channel_config c = {};
  // Common options
  channel_config_set_ring(&c, false, 0);
  channel_config_set_bswap(&c, false);
  channel_config_set_irq_quiet(&c, false);
  channel_config_set_enable(&c, true);
  channel_config_set_sniff_enable(&c, false);
  channel_config_set_high_priority(&c, false);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);

  // TX
  channel_config_set_read_increment(&c, false);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, DREQ_FORCE);
  channel_config_set_chain_to(&c, c1_channel);  // self-chain disables it
  dma_channel_set_config(c1_channel, &c, false);
  dma_channel_set_read_addr(c1_channel, &c1_read, false);
  dma_channel_set_write_addr(c1_channel, &c1_write, false);

  // RX
  channel_config_set_read_increment(&c, false);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, DREQ_FORCE);
  channel_config_set_chain_to(&c, c2_channel);  // self-chain disables it
  dma_channel_set_config(c2_channel, &c, false);
  dma_channel_set_read_addr(c2_channel, &c2_read, false);
  dma_channel_set_write_addr(c2_channel, &c2_write, false);

  // IRQs
  dma_irqn_set_channel_enabled(irq_index, c1_channel, true);
  dma_irqn_set_channel_enabled(irq_index, c2_channel, true);
  irq_set_exclusive_handler(irq_index ? DMA_IRQ_1 : DMA_IRQ_0, &DMA_ISR);
  irq_set_enabled(irq_index ? DMA_IRQ_1 : DMA_IRQ_0, true);

  for (;;) {
    dma_channel_set_trans_count(c1_channel, transfer_count, false);
    dma_channel_set_trans_count(c2_channel, transfer_count, false);

    // start next cycle
    gpio_put(debug_gpio_start, !gpio_get(debug_gpio_start));
    dma_start_channel_mask((1u << c1_channel) | (1u << c2_channel));

    // wait for it to finish
    while (dma_irqn_get_channel_status(irq_index, c1_channel) || dma_irqn_get_channel_status(irq_index, c2_channel) || dma_channel_is_busy(c1_channel) || dma_channel_is_busy(c2_channel)) {
      // busy-wait for both channels to finish
    }
  }
  return 0;
}
