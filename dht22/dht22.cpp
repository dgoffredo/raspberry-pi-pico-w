#include <picoro/coroutine.h>
#include "dht22.h"
#include <picoro/event_loop.h>
#include <picoro/sleep.h>

#include <pico/async_context_poll.h>
#include <pico/stdio.h>

#include <cassert>
#include <chrono>

picoro::Coroutine<void> monitor_sensor(
    async_context_t *ctx,
    picoro::dht22::Driver *driver,
    PIO pio,
    uint8_t gpio_pin,
    const char *name) {
  picoro::dht22::Sensor sensor(driver, pio, gpio_pin);
  for (;;) {
    co_await picoro::sleep_for(ctx, std::chrono::seconds(2));
    float celsius, humidity_percent;
    const int rc = co_await sensor.measure(&celsius, &humidity_percent);
    if (rc == 0) {
      std::printf("%s: %.1f C, %.1f%% humidity\n", name, celsius,
              humidity_percent);
    }
  }
}

int main() {
    stdio_init_all();

    async_context_poll_t context;
    const bool ok = async_context_poll_init_with_defaults(&context);
    assert(ok);
    async_context_t *const ctx = &context.core;

    constexpr int which_dma_irq = 0;
    picoro::dht22::Driver driver(ctx, which_dma_irq);

    picoro::run_event_loop(ctx,
        monitor_sensor(ctx, &driver, pio0, 15, "top shelf"),
        monitor_sensor(ctx, &driver, pio0, 16, "middle shelf"),
        monitor_sensor(ctx, &driver, pio0, 22, "bottom shelf"));

    // unreachable
    async_context_deinit(ctx);
}
