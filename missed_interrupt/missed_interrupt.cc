#include <cstdio>
#include <cstring>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "pico/time.h"
#include "pico/sync.h"
#include "pico/stdio.h"

#if LIB_PICO_STDIO_UART
#error no
#endif
#if LIB_PICO_STDIO_USB
#error no
#endif
#if LIB_PICO_STDIO_SEMIHOSTING
#error no
#endif


int tx_channel;
int rx_channel;

const int spi_frequency = 4'000'000;

const int irq_index = 1;

const int debug_gpio_missed = 5;
const int debug_gpio_tx = 6;
const int debug_gpio_rx = 7;
const int debug_gpio_start = 8;
const int debug_gpio_irq = 9;

volatile bool got_tx = 0;
volatile bool got_rx = 0;


void __not_in_flash_func(DMA_ISR)() {
  // 7: ok
  // 9: not ok
  //gpio_put(debug_gpio_irq, !gpio_get(debug_gpio_irq));
  //gpio_put(debug_gpio_irq, !gpio_get(debug_gpio_irq));
  busy_wait_us(10);
  if (dma_hw->ints1 & (1<<rx_channel)) {
    gpio_put(debug_gpio_rx, !gpio_get(debug_gpio_rx));
  }
  if (dma_hw->ints1 & (1<<tx_channel)) {
    gpio_put(debug_gpio_tx, !gpio_get(debug_gpio_tx));
  }
  gpio_put(debug_gpio_irq, dma_hw->ints1);
  hw_set_bits(&dma_hw->ints1, 0);
  gpio_put(debug_gpio_irq, dma_hw->ints1);
  return;

  if (dma_hw->ints1 & (1<<rx_channel)) {
    gpio_put(debug_gpio_rx, !gpio_get(debug_gpio_rx));
    // Direct assignment works correctly; hw_*_bits doesn't
    //dma_irqn_acknowledge_channel(irq_index, rx_channel);
    //hw_clear_bits(&dma_hw->ints1, 1u << rx_channel);
    //dma_hw->ints1 = 1u << rx_channel;
    hw_set_bits(&dma_hw->ints1, 0);
    gpio_put(debug_gpio_irq, dma_hw->ints1);
    got_rx = 1;
  }
  if (dma_hw->ints1 & (1<<tx_channel)) {
    gpio_put(debug_gpio_tx, !gpio_get(debug_gpio_tx));
    // Direct assignment works correctly; hw_*_bits doesn't
    //dma_irqn_acknowledge_channel(irq_index, tx_channel);
    //hw_clear_bits(&dma_hw->ints1, 1u << tx_channel);
    //dma_hw->ints1 = 1u<<tx_channel;
    hw_set_bits(&dma_hw->ints1, 0);
    gpio_put(debug_gpio_irq, dma_hw->ints1);
    got_tx = 1;
  }
  //gpio_put(debug_gpio_irq, !gpio_get(debug_gpio_irq));
}

int main() {
  busy_wait_ms(100);

  gpio_init(debug_gpio_missed);
  gpio_init(debug_gpio_tx);
  gpio_init(debug_gpio_rx);
  gpio_init(debug_gpio_start);
  gpio_init(debug_gpio_irq);
  gpio_set_dir(debug_gpio_missed, GPIO_OUT);
  gpio_set_dir(debug_gpio_tx, GPIO_OUT);
  gpio_set_dir(debug_gpio_rx, GPIO_OUT);
  gpio_set_dir(debug_gpio_start, GPIO_OUT);
  gpio_set_dir(debug_gpio_irq, GPIO_OUT);

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

  dma_irqn_set_channel_enabled(irq_index, tx_channel, true);
  dma_irqn_set_channel_enabled(irq_index, rx_channel, true);

  irq_set_exclusive_handler(irq_index ? DMA_IRQ_1 : DMA_IRQ_0, &DMA_ISR);
  irq_set_priority(irq_index ? DMA_IRQ_1 : DMA_IRQ_0, 128);
  irq_set_enabled(irq_index ? DMA_IRQ_1 : DMA_IRQ_0, true);

  uint32_t num_transfers = 0;
  for (;;) {
    // start next cycle
    ++num_transfers;
    got_tx = 0;
    got_rx = 0;
    dma_channel_set_read_addr(tx_channel, tx_read, false);
    dma_channel_set_write_addr(rx_channel, rx_write, false);
    dma_channel_set_trans_count(tx_channel, transfer_count, false);
    dma_channel_set_trans_count(rx_channel, transfer_count, false);
    gpio_put(debug_gpio_start, !gpio_get(debug_gpio_start));
    dma_start_channel_mask((1u << tx_channel) | (1u << rx_channel));

    // wait for it to finish
    const uint32_t max_wait = 500;
    uint32_t wait_time = 0;
    //while (!got_tx || !got_rx || dma_irqn_get_channel_status(irq_index, tx_channel) || dma_irqn_get_channel_status(irq_index, rx_channel) || dma_hw->intr || dma_channel_is_busy(tx_channel) || dma_channel_is_busy(rx_channel) || spi_is_busy(spi1)) {
    while (!got_tx || !got_rx || dma_irqn_get_channel_status(irq_index, tx_channel) || dma_irqn_get_channel_status(irq_index, rx_channel) || dma_channel_is_busy(tx_channel) || dma_channel_is_busy(rx_channel) || spi_is_busy(spi1)) {
        if(wait_time++ > max_wait) break;
    }
    if (wait_time > max_wait) {
        gpio_put(debug_gpio_missed, !gpio_get(debug_gpio_missed));
    }
  }

  return 0;
}
