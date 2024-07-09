#include "pico/async_context_poll.h"
#include "pico/binary_info.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <tusb.h>

#include <stdio.h>
#include <chrono>

#include "picoro.h"

#define CHECKPOINT() printf("[%s:%d]\n", __FILE__, __LINE__)

// Wait for the host to attach to the USB terminal (i.e. ttyACM0).
// Blink the onboard LED while we're waiting.
// Give up after the specified number of seconds.
picoro::Coroutine wait_for_usb_debug_attach(async_context_t *context, std::chrono::seconds timeout) {
    const int iterations = timeout / std::chrono::seconds(1);
    for (int i = 0; i < iterations && !tud_cdc_connected(); ++i) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        co_await picoro::sleep_for(context, std::chrono::milliseconds(500));
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        co_await picoro::sleep_for(context, std::chrono::milliseconds(500));
    }

    co_await picoro::sleep_for(context, std::chrono::seconds(1));
    printf("Glad you could make it.\n");
}

picoro::Coroutine do_loopy_thing(async_context_t *context) {
    for (int i = 0; ; ++i) {
        printf("coroutine tick %d\n", i);
        co_await picoro::sleep_for(context, std::chrono::seconds(2));
    }
}

picoro::Coroutine coroutine_main(async_context_t *context) {
    co_await wait_for_usb_debug_attach(context, std::chrono::seconds(10));
    co_await do_loopy_thing(context);
}

int main() {
    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    async_context_poll_t context = {};
    bool succeeded = async_context_poll_init_with_defaults(&context);
    if (!succeeded) {
        printf("Failed to initialize async_context_poll_t\n");
        return -2;
    }

    run_event_loop(&context.core, coroutine_main(&context.core));

    // unreachable
    async_context_deinit(&context.core);
}
