// #include "hardware/i2c.h"
#include "pico/async_context_poll.h"
#include "pico/binary_info.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <tusb.h>

// #include "scd4x_i2c.h"
#include <stdio.h>
#include <chrono>
#include <coroutine>

#define CHECKPOINT() printf("[%s:%d]\n", __FILE__, __LINE__)

// Wait for the host to attach to the USB terminal (i.e. ttyACM0).
// Blink the onboard LED while we're waiting.
// Give up after the specified number of seconds.
void wait_for_usb_debug_attach(std::chrono::seconds timeout) {
    const int iterations = timeout / std::chrono::seconds(1);
    for (int i = 0; i < iterations && !tud_cdc_connected(); ++i) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(500);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(500);
    }
}

int main() {
    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    wait_for_usb_debug_attach(std::chrono::seconds(10));
    
    async_context_poll_t context = {};
    bool succeeded = async_context_poll_init_with_defaults(&context);
    if (!succeeded) {
        printf("Failed to initialize async_context_poll_t\n");
        return -2;
    }

    // TODO
    for (;;) {
        printf("Performing any work enqueued onto the async_context_t\n");
        async_context_poll(&context.core);
        printf("Waiting up to one second for work to appear on the async_context_t\n");
        async_context_wait_for_work_ms(&context.core, 1000);
    }

    async_context_deinit(&context.core);
}
