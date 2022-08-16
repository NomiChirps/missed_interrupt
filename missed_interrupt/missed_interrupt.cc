#include <cstdio>
#include <cstring>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "pico/stdio.h"
#include "pico/time.h"

int tx_channel;
int rx_channel;

const int spi_frequency = 8'000'000;

const int irq_index = 1;

volatile bool got_tx = 0;
volatile bool got_rx = 0;

const int debug_gpio_irq = 5;
const int debug_gpio_tx = 6;
const int debug_gpio_rx = 7;
const int debug_gpio_start = 8;

// Straightforward: checks one channel, acks it, then the other.
void DMA_ISR() {
  gpio_put(debug_gpio_irq, 1);
  if (dma_irqn_get_channel_status(irq_index, tx_channel)) {
    gpio_put(debug_gpio_tx, !gpio_get(debug_gpio_tx));
    dma_irqn_acknowledge_channel(irq_index, tx_channel);
    got_tx = 1;
  }
  if (dma_irqn_get_channel_status(irq_index, rx_channel)) {
    gpio_put(debug_gpio_rx, !gpio_get(debug_gpio_rx));
    dma_irqn_acknowledge_channel(irq_index, rx_channel);
    got_rx = 1;
  }
  // putting barriers doesn't help
  __asm("dmb");
  __asm("isb");
  gpio_put(debug_gpio_irq, 0);
}

int main() {
  stdio_init_all();
  printf("hello\n");

  gpio_init(debug_gpio_irq);
  gpio_init(debug_gpio_tx);
  gpio_init(debug_gpio_rx);
  gpio_init(debug_gpio_start);
  gpio_set_dir(debug_gpio_irq, GPIO_OUT);
  gpio_set_dir(debug_gpio_tx, GPIO_OUT);
  gpio_set_dir(debug_gpio_rx, GPIO_OUT);
  gpio_set_dir(debug_gpio_start, GPIO_OUT);

  spi_init(spi1, spi_frequency);
  spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  gpio_set_function(10, GPIO_FUNC_SPI);
  gpio_set_function(11, GPIO_FUNC_SPI);
  gpio_set_function(12, GPIO_FUNC_SPI);
  // sclk = 10
  // mosi = 11
  // miso = 12

  // spi_frequency / 1'000'000 seems to trigger it reliably
  const int transfer_count = 4;

  uint8_t dummy1[transfer_count];
  uint8_t dummy2[transfer_count];
  volatile void* tx_read = &dummy1;
  volatile void* tx_write = &spi_get_hw(spi1)->dr;
  volatile void* rx_read = &spi_get_hw(spi1)->dr;
  volatile void* rx_write = &dummy2;

  memset(dummy1, 0x55, transfer_count);
  memset(dummy2, 0x55, transfer_count);

  tx_channel = dma_claim_unused_channel(true);
  rx_channel = dma_claim_unused_channel(true);

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
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, spi_get_dreq(spi1, true));
  channel_config_set_chain_to(&c, tx_channel);  // self-chain disables it
  dma_channel_set_config(tx_channel, &c, false);
  dma_channel_set_read_addr(tx_channel, tx_read, false);
  dma_channel_set_write_addr(tx_channel, tx_write, false);
  dma_channel_set_trans_count(tx_channel, transfer_count, false);

  // RX
  channel_config_set_read_increment(&c, false);
  channel_config_set_write_increment(&c, true);
  channel_config_set_dreq(&c, spi_get_dreq(spi1, false));
  channel_config_set_chain_to(&c, rx_channel);  // self-chain disables it
  dma_channel_set_config(rx_channel, &c, false);
  dma_channel_set_read_addr(rx_channel, rx_read, false);
  dma_channel_set_write_addr(rx_channel, rx_write, false);
  dma_channel_set_trans_count(rx_channel, transfer_count, false);

  printf("channels configured\n");

  dma_irqn_set_channel_enabled(irq_index, tx_channel, true);
  dma_irqn_set_channel_enabled(irq_index, rx_channel, true);

  irq_set_exclusive_handler(irq_index ? DMA_IRQ_1 : DMA_IRQ_0, &DMA_ISR);
  irq_set_enabled(irq_index ? DMA_IRQ_1 : DMA_IRQ_0, true);

  printf("irq configured\n");

  uint32_t num_transfers = 0;
  for (;;) {
    if (!dma_channel_is_busy(tx_channel) && !dma_channel_is_busy(rx_channel) &&
        !spi_is_busy(spi1)) {
      // Even if we make SURE to wait for both interrupts to finish
      // before starting the next transfer, we lose some.
      uint32_t wait_time = 0;
      while (!got_tx || !got_rx) {
        if (++wait_time > 1'000'000) break;
      }
      if (wait_time > 1'000'000) {
        printf("at iteration %ld got_tx=%d got_rx=%d\n", num_transfers, got_tx, got_rx);
      }
      if (num_transfers % (spi_frequency/20) == 0) {
        printf("iteration %ld\n", num_transfers);
      }
 
      // start next cycle
      ++num_transfers;
      got_tx = 0;
      got_rx = 0;
      dma_channel_set_read_addr(tx_channel, tx_read, false);
      dma_channel_set_write_addr(rx_channel, rx_write, false);
      dma_channel_set_trans_count(tx_channel, transfer_count, false);
      dma_channel_set_trans_count(rx_channel, transfer_count, false);
      // putting barriers doesn't help
      __asm("dmb");
      __asm("isb");
      gpio_put(debug_gpio_start, !gpio_get(debug_gpio_start));
      dma_start_channel_mask((1u << tx_channel) | (1u << rx_channel));
    }
  }

  return 0;
}
