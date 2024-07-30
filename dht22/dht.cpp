#include "dht.h"
#include <dht-pio/dht.h>

#include <stdlib.h>
#include <malloc.h>

#include <chrono>

#include <pico/async_context_poll.h>
#include <pico/cyw43_arch.h>

#include <picoro/coroutine.h>
#include <picoro/event_loop.h>
#include <picoro/sleep.h>


uint32_t getTotalHeap() {
   extern char __StackLimit, __bss_end__;
   return &__StackLimit  - &__bss_end__;
}

uint32_t getFreeHeap() {
   struct mallinfo m = mallinfo();
   return getTotalHeap() - m.uordblks;
}

picoro::Coroutine<void> monitor_memory(async_context_t *context) {
    for (;;) {
        const uint32_t bytes_free = getFreeHeap();
        printf("The heap has %lu bytes (%lu KB) free.\n", bytes_free, bytes_free / 1000UL);
        co_await picoro::sleep_for(context, std::chrono::seconds(5));
    }
}

constexpr dht_model_t DHT_MODEL = DHT22;
constexpr uint DATA_PIN = 16;

float celsius_to_fahrenheit(float temperature) {
    return temperature * (9.0f / 5) + 32;
}

/* TODO

int dma_chan;

void dma_irq0_handler() {
    // TODO: thing
    constexpr uint which_dma_irq = 0;
    dma_irqn_acknowledge_channel(which_dma_irq, dma_chan);
}

...

    // Tell the DMA to raise IRQ line 0 when the channel finishes a block
    dma_channel_set_irq0_enabled(dma_chan, true);

    // Configure the processor to run dma_handler() when DMA IRQ 0 is asserted
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);
*/

picoro::Coroutine<void> pio_main(async_context_t *context) {
    dht_t dht;
    dht_init(&dht, DHT_MODEL, pio0, DATA_PIN, true /* pull_up */);
    for (int i = 0; i < 10; ++i) {
        dht_start_measurement(&dht);

        float humidity;
        float temperature_c;
        dht_result_t result = dht_finish_measurement_blocking(&dht, &humidity, &temperature_c);
        if (result == DHT_RESULT_OK) {
            printf("%.1f C (%.1f F), %.1f%% humidity\n", temperature_c, celsius_to_fahrenheit(temperature_c), humidity);
        } else if (result == DHT_RESULT_TIMEOUT) {
            puts("DHT sensor not responding. Please check your wiring.");
        } else {
            assert(result == DHT_RESULT_BAD_CHECKSUM);
            puts("Bad checksum");
        }

        co_await picoro::sleep_for(context, std::chrono::seconds(2));
    }
    dht_deinit(&dht);
}

int main() {
    stdio_init_all();

    async_context_poll_t context;
    if (!async_context_poll_init_with_defaults(&context)) {
        printf("Failed to initialize async_context_poll_t\n");
        return -1;
    }

    // Do some WiFi chip setup here, just so that we can use the LED
    // immediately. The rest of the setup happens in `networking`.
    cyw43_arch_set_async_context(&context.core);
    // 🇺🇸 🦅
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        printf("failed to initialize WiFi\n");
        return -2;
    }

    picoro::run_event_loop(&context.core,
        // dht::demo(&context.core),
        monitor_memory(&context.core),
        pio_main(&context.core));

    // unreachable
    cyw43_arch_deinit();
    async_context_deinit(&context.core);
}
