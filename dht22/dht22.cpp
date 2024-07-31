#include "dht22.h"
#include "dht22.pio.h"
#include <dht-pio/dht.h>

#include <stdlib.h>
#include <malloc.h>

#include <chrono>

#include <pico/async_context_poll.h>
#include <pico/cyw43_arch.h>

#include <hardware/clocks.h>
#include <hardware/dma.h>

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


struct DMAInterrupt {
    static uint channel;
    static async_context_t *context;
    static async_when_pending_worker_t worker;
    static std::coroutine_handle<> continuation;

    static void irq0_handler() {
        async_context_set_work_pending(DMAInterrupt::context, &DMAInterrupt::worker);

        const uint which_dma_irq = 0;
        dma_irqn_acknowledge_channel(which_dma_irq, DMAInterrupt::channel);
    }

    static void setup(async_context_t *context, dht_t *dht) {
        DMAInterrupt::context = context;
        DMAInterrupt::worker.do_work = &DMAInterrupt::do_work;
        async_context_add_when_pending_worker(context, &DMAInterrupt::worker);

        DMAInterrupt::channel = dht->dma_chan;
        dma_channel_set_irq0_enabled(DMAInterrupt::channel, true);
        irq_add_shared_handler(DMA_IRQ_0, &DMAInterrupt::irq0_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
        irq_set_enabled(DMA_IRQ_0, true);
    }

    static void do_work(async_context_t*, async_when_pending_worker_t*) {
        DMAInterrupt::continuation.resume();
    }

    struct Awaiter {
        bool await_ready() {
            return false;
        }

        bool await_suspend(std::coroutine_handle<> continuation) {
            DMAInterrupt::continuation = continuation;
            return true;
        }

        void await_resume() {}
    };
};

uint DMAInterrupt::channel = 0;
async_context_t *DMAInterrupt::context = nullptr;
async_when_pending_worker_t DMAInterrupt::worker = {};
std::coroutine_handle<> DMAInterrupt::continuation;

picoro::Coroutine<dht_result_t> dht_finish_measurement(dht_t *dht, float *humidity, float *temperature_c) {
    assert(dht->pio != NULL); // not initialized
    // assert(pio_sm_is_enabled(dht->pio, dht->sm)); // no measurement in progress

    co_await DMAInterrupt::Awaiter{};

    pio_sm_set_enabled(dht->pio, dht->sm, false);
    // make sure pin is left in hi-z mode
    pio_sm_exec(dht->pio, dht->sm, pio_encode_set(pio_pindirs, 0));

    const uint8_t checksum = dht->data[0] + dht->data[1] + dht->data[2] + dht->data[3];
    if (dht->data[4] != checksum) {
        co_return DHT_RESULT_BAD_CHECKSUM;
    }
    // #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    *humidity = decode_humidity(dht->model, dht->data[0], dht->data[1]);
    *temperature_c = decode_temperature(dht->model, dht->data[2], dht->data[3]);
    co_return DHT_RESULT_OK;
}

void dht22_measure_blocking(PIO pio, uint state_machine, float *celsius, float *humidity_percent) {
    // Push some counters to the state machine. It will put them in its x and y
    // registers. We pack them together as two 16-bit values in one 32-bit
    // word.
    // This also synchronizes us with the state machine.
    const uint32_t initial_request_cycles = 1000;
    const uint32_t bits_to_receive = 40;
    const uint32_t payload = (initial_request_cycles << 16) | bits_to_receive;

    pio_sm_put_blocking(pio, state_machine, payload);

    // TODO
    (void)celsius;
    (void)humidity_percent;

    for (int i = 0; i < 5; ++i) {
        const uint32_t byte = pio_sm_get_blocking(pio, state_machine);
        printf("received byte %d: %02lx\n", i, byte);
    }
}

// The specified `pio` has already been set up for use with a DHT22 by calling
// `init_dht22_pio`. Claim one of the PIO's state machines, configure it, start
// it, and return the state machine.
uint init_dht22_state_machine(PIO pio, uint program_offset, uint8_t gpio_pin) {
    const bool required = true;
    const uint state_machine = pio_claim_unused_sm(pio, required);
    // TODO dht->dma_chan = dma_claim_unused_channel(required);
    // TODO dht->gpio_pin = gpio_pin;

    pio_gpio_init(pio, gpio_pin);
    gpio_pull_up(gpio_pin);

    // `dht22_program_get_default_config` is defined in the build-generated
    // header `dht22.pio.h`.
    pio_sm_config cfg = dht22_program_get_default_config(program_offset);

    const float desired_pio_hz = 1'000'000;
    sm_config_set_clkdiv(&cfg, clock_get_hz(clk_sys) / desired_pio_hz);
    // The things we do with the GPIO pin is "set" its value, "jmp"
    // based on its value, and "wait" on it.
    const uint count = 1;
    sm_config_set_set_pins(&cfg, gpio_pin, count); // for "set"
    sm_config_set_jmp_pin(&cfg, gpio_pin); // for "jmp"
    sm_config_set_in_pins(&cfg, gpio_pin); // for "wait"

    // Bits arrive in most-significant-bit-first (MSB) order and are shifted
    // into the input shift register (ISR) leftward.
    // Push (autopush) data to the input FIFO after every 8 bits; this also
    // clears the ISR.
    // We autopush on every byte because the total amount of bits received is
    // one word (the temperature/humidity data) plus one byte (the checksum).
    // Anything larger would leave the checksum byte in the ISR.
    const bool shift_direction = false; // false means left
    const bool autopush = true;
    const uint autopush_threshold_bits = 8;
    sm_config_set_in_shift(&cfg, shift_direction, autopush, autopush_threshold_bits);
    pio_sm_init(pio, state_machine, program_offset, &cfg);

    // 🐎 🐎 🐎 🐎
    pio_sm_set_enabled(pio, state_machine, true);
    return state_machine;
}

// Load the DHT22 program into `pio` and return the program offset within the
// PIO's instruction memory.
uint init_dht22_pio(PIO pio) {
    // `dht22_program` is defined in the build-generated header `dht22.pio.h`.
    return pio_add_program(pio, &dht22_program);
}

picoro::Coroutine<void> pio_main(async_context_t *context) {
    /* TODO: trying my program instead of this.
    dht_t dht;
    const bool pull_up = true;
    dht_init(&dht, DHT_MODEL, pio0, DATA_PIN, pull_up);
    DMAInterrupt::setup(context, &dht);

    // for (int i = 0; i < 10; ++i) {
    for (;;) {
        co_await picoro::sleep_for(context, std::chrono::seconds(2));
        dht_start_measurement(&dht);

        float humidity;
        float temperature_c;
        dht_result_t result = co_await dht_finish_measurement(&dht, &humidity, &temperature_c);
        if (result == DHT_RESULT_OK) {
            printf("%.1f C (%.1f F), %.1f%% humidity\n", temperature_c, celsius_to_fahrenheit(temperature_c), humidity);
        } else {
            assert(result == DHT_RESULT_BAD_CHECKSUM);
            puts("Bad checksum");
        }
    }

    dht_deinit(&dht);
    */
    PIO pio = pio0;
    const uint program_offset = init_dht22_pio(pio);
    const uint8_t gpio_pin = DATA_PIN;
    const uint state_machine = init_dht22_state_machine(pio, program_offset, gpio_pin);
    for (;;) {
        co_await picoro::sleep_for(context, std::chrono::seconds(2));
        dht22_measure_blocking(pio, state_machine, nullptr, nullptr);
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

    picoro::run_event_loop(&context.core,
        // dht::demo(&context.core),
        monitor_memory(&context.core),
        pio_main(&context.core));

    // unreachable
    // TODO: no WiFi for now: cyw43_arch_deinit();
    async_context_deinit(&context.core);
}
