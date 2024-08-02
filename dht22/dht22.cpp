#include "dht22.h"

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
    for (int i = 0; ; ++i) {
        const uint32_t bytes_free = getFreeHeap();
        printf("%d: The heap has %lu bytes (%lu KB) free.\n", i, bytes_free, bytes_free / 1000UL);
        co_await picoro::sleep_for(context, std::chrono::seconds(5));
    }
}

float celsius_to_fahrenheit(float temperature) {
    return temperature * (9.0f / 5) + 32;
}

picoro::Coroutine<void> monitor_sensor(async_context_t *context, std::chrono::seconds initial_delay, picoro::dht22::Driver& driver, int gpio_pin) {
    co_await picoro::sleep_for(context, initial_delay);
    picoro::dht22::Sensor sensor(&driver, pio0, gpio_pin);
    for (int i = 0; i < 10; ++i) {
        // printf("%d: Before sleep\n", i);
        co_await picoro::sleep_for(context, std::chrono::seconds(2));
        // printf("%d: After sleep\n", i);
        float celsius;
        float humidity_percent;
        // printf("%d: Before outer await\n", i);
        const int rc = co_await sensor.measure(&celsius, &humidity_percent);
        // printf("%d: After outer await\n", i);
        if (rc) {
            printf("gpio %d iteration %d: BAD THINGS BAD THINGS\n", gpio_pin, i);
        } else {
            printf("gpio %d iteration %d: %.1f C (%.1f F), %.1f%% humidity\n", gpio_pin, i, celsius, celsius_to_fahrenheit(celsius), humidity_percent);
        }
    }
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
    /* TODO: no WiFi for now.
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        printf("failed to initialize WiFi\n");
        return -2;
    }
    */

    constexpr int which_dma_irq = 0;
    picoro::dht22::Driver driver(&context.core, which_dma_irq);

    picoro::run_event_loop(&context.core,
        monitor_memory(&context.core),
        monitor_sensor(&context.core, std::chrono::seconds(1), driver, 15),
        monitor_sensor(&context.core, std::chrono::seconds(2), driver, 16),
        monitor_sensor(&context.core, std::chrono::seconds(3), driver, 22));

    // unreachable
    // TODO: no WiFi for now: cyw43_arch_deinit();
    async_context_deinit(&context.core);
}
