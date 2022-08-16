#include <cstdio>
#include <cstring>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"

int tx_channel;
int rx_channel;

// At 100'000, imbalance is stable at zero.
// At 1'000'000, it slowly grows.
// At 8'000'000, stable.
// At 16'000'000, stable.
const int spi_frequency = 1'000'000;

const int irq_index = 1;

volatile uint32_t tx_interrupt_count = 0;
volatile uint32_t rx_interrupt_count = 0;
volatile int32_t tx_vs_rx_count = 0;

const int debug_gpio_irq = 5;
const int debug_gpio_tx = 6;
const int debug_gpio_rx = 7;

void DMA_ISR() {
  gpio_put(debug_gpio_irq, 1);
  bool tx_irq = dma_irqn_get_channel_status(irq_index, tx_channel);
  // bool rx_irq = dma_hw->intr & (1u << head->rx);
  bool rx_irq = dma_irqn_get_channel_status(irq_index, rx_channel);
  if (tx_irq) {
    gpio_put(debug_gpio_tx, !gpio_get(debug_gpio_tx));
    dma_irqn_acknowledge_channel(irq_index, tx_channel);
    tx_interrupt_count++;
    tx_vs_rx_count++;
  }
  if (rx_irq) {
    gpio_put(debug_gpio_rx, !gpio_get(debug_gpio_rx));
    dma_irqn_acknowledge_channel(irq_index, rx_channel);
    rx_interrupt_count++;
    tx_vs_rx_count--;
  }
  gpio_put(debug_gpio_irq, 0);
}

int main() {
  stdio_usb_init();
  printf("hello\n");

  gpio_init(debug_gpio_irq);
  gpio_init(debug_gpio_tx);
  gpio_init(debug_gpio_rx);
  gpio_set_dir(debug_gpio_irq, GPIO_OUT);
  gpio_set_dir(debug_gpio_tx, GPIO_OUT);
  gpio_set_dir(debug_gpio_rx, GPIO_OUT);

  spi_init(spi1, spi_frequency);
  spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  gpio_set_function(10, GPIO_FUNC_SPI);
  gpio_set_function(11, GPIO_FUNC_SPI);
  gpio_set_function(12, GPIO_FUNC_SPI);
  // sclk = 10
  // mosi = 11
  // miso = 12

  const int transfer_count = 1;
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
  channel_config_set_read_increment(&c, false);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, spi_get_dreq(spi1, true));
  channel_config_set_chain_to(&c, tx_channel);  // self-chain disables it
  dma_channel_set_config(tx_channel, &c, false);
  dma_channel_set_read_addr(tx_channel, tx_read, false);
  dma_channel_set_write_addr(tx_channel, tx_write, false);
  dma_channel_set_trans_count(tx_channel, transfer_count, false);

  // RX
  channel_config_set_read_increment(&c, false);
  channel_config_set_write_increment(&c, false);
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

  uint32_t n = 0;
  int32_t min_imbalance = INT32_MAX;
  int32_t max_imbalance = INT32_MIN;
  for (;;) {
    if (!dma_channel_is_busy(tx_channel) && !dma_channel_is_busy(rx_channel) &&
        !spi_is_busy(spi1)) {
      int32_t imbalance = tx_vs_rx_count;
      if (imbalance < min_imbalance) min_imbalance = imbalance;
      if (imbalance > max_imbalance) max_imbalance = imbalance;
      if (++n % (spi_frequency / 50) == 0) {
        printf("iteration %ld, imbalance: %ld, min %ld max %ld\n", n, imbalance,
               min_imbalance, max_imbalance);
      }
      // start next cycle
      dma_channel_set_read_addr(tx_channel, tx_read, false);
      dma_channel_set_write_addr(rx_channel, rx_write, false);
      dma_channel_set_trans_count(tx_channel, transfer_count, false);
      dma_channel_set_trans_count(rx_channel, transfer_count, false);
      dma_start_channel_mask((1u << tx_channel) | (1u << rx_channel));
    }
  }

  return 0;
}