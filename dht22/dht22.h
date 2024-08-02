#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <coroutine>
#include <cstdio>

#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>

#include <pico/async_context.h>
#include <pico/stdlib.h>

#include <picoro/coroutine.h>
#include <picoro/sleep.h>

#include "dht22.pio.h"

namespace picoro {
namespace dht22 {

struct Driver;
struct Sensor;

// TODO: PIO management

struct Driver {
    async_context_t *context;
    async_when_pending_worker_t worker;
    std::array<Sensor*, 8> sensors;
    std::array<int, 2> program_offsets; // [in PIO0, in PIO1]. -1 means N/A.
    uint8_t which_dma_irq : 1;
    bool irq_previously_enabled : 1;

    explicit Driver(async_context_t*, uint8_t which_dma_irq);
    ~Driver();
    // You can't copy or move a Driver, because its `.worker` member is
    // referenced by an event loop (`.context`).
    Driver(const Driver&) = delete;
    Driver(Driver&&) = delete;

    int load(PIO);
};

struct Sensor {
    std::array<char, 5> data;
    uint8_t which_pio : 1;
    uint8_t state_machine : 2;
    uint8_t dma_channel : 4;
    bool ready : 1;
    std::coroutine_handle<> continuation;
    Driver *driver;

    explicit Sensor(Driver *, PIO, int gpio_pin);
    ~Sensor();
    // You can't copy or move a Sensor, because its `.data` member is
    // referenced by a DMA channel.
    Sensor(const Sensor&) = delete;
    Sensor(Sensor&&) = delete;

    PIO get_pio() const;
    void set_pio(PIO);

    Coroutine<int> measure(float *celsius, float *humidity_percent);
};

// When a `Driver` is instantiated, it installs itself here via
// `core_context[get_core_num()] = this;`.
// When a `Driver` is destroyed, it uninstalls itself via
// `core_context[get_core_num()] = nullptr;`.
inline
Driver *core_context[2] = {};

template <uint8_t which_dma_irq>
void dma_irq_handler() {
    Driver *driver = core_context[get_core_num()];
    if (driver == nullptr) {
        // TODO: spurious?
        return;
    }

    for (Sensor *sensor : driver->sensors) {
        if (sensor == nullptr) {
          // This sensor isn't in use.
          continue;
        }
        if (!dma_irqn_get_channel_status(which_dma_irq, sensor->dma_channel)) {
          // This sensor's DMA channel wasn't the cause of this interrupt.
          continue;
        }

        // We found the cause of the interrupt. Acknowledge the interrupt, mark
        // the sensor as "ready," and schedule the Driver's handler for
        // execution on the event loop. The handler will run the continuations
        // of any ready sensors.
        dma_irqn_acknowledge_channel(which_dma_irq, sensor->dma_channel);
        sensor->ready = true;
        async_context_set_work_pending(driver->context, &driver->worker);
        return;
    }
}

inline
void handle_ready_sensors(async_context_t *, async_when_pending_worker_t *worker) {
  auto *driver = static_cast<Driver*>(worker->user_data);
  for (Sensor *sensor : driver->sensors) {
      if (sensor == nullptr || !sensor->ready) {
        continue;
      }
      sensor->ready = false;
      // If the coroutine responsible for this sensor suspended itself waiting
      // for data, then `sensor->continuation` will hold a handle to that
      // coroutine.  However, if the DMA channel finished before the coroutine
      // tried to suspend, then the coroutine will not have suspended. In that
      // case, `sensor->continuation` will be null, but the DMA IRQ still fires
      // and sets `sensor->ready = true`.
      // To avoid resuming an invalid coroutine handle, always set
      // `sensor->continuation = nullptr` before resuming the handle.
      if (auto continuation = sensor->continuation) {
          sensor->continuation = nullptr;
          continuation.resume();
      }
  }
}

inline
float decode_temperature(uint8_t b0, uint8_t b1) {
    float temperature = 0.1f * (((b0 & 0x7F) << 8) + b1);
    if (b0 & 0x80) {
        temperature = -temperature;
    }
    return temperature;
}

inline
float decode_humidity(uint8_t b0, uint8_t b1) {
    return 0.1f * ((b0 << 8) + b1);
}

inline
Driver::Driver(async_context_t *context, uint8_t which_dma_irq)
: context(context)
, worker()
, sensors()
, program_offsets({-1, -1})
, which_dma_irq(which_dma_irq) {
    std::printf("dht22::Driver::Driver\n");
    // Here's the plan:
    // - Register ourselves in `core_context`.
    // - Register `worker` with `context`.
    // - Register the appropriate DMA IRQ handler.

    const uint core = get_core_num();
    if (core_context[core]) {
        panic("In dht22::Driver constructor: At most one Driver per core is allowed.");
    }
    core_context[core] = this;

    worker.do_work = &handle_ready_sensors;
    worker.user_data = this;
    async_context_add_when_pending_worker(context, &worker);

    const auto irq = which_dma_irq ? DMA_IRQ_1 : DMA_IRQ_0;
    const auto handler = which_dma_irq ? &dma_irq_handler<1> : &dma_irq_handler<0>;
    irq_add_shared_handler(
        irq,
        handler,
        PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_previously_enabled = irq_is_enabled(irq);
    irq_set_enabled(irq, true);
}

Driver::~Driver() {
    std::printf("dht22::Driver::~Driver\n");
    // Undo what we did in the constructor:
    // - Unregister ourselves in `core_context`.
    // - Unregister `worker` with `context`.
    // - Unregister the DMA IRQ handler.
    // Also, remove the PIO program from the PIO devices if the program was
    // previously loaded there.

    const uint core = get_core_num();
    if (core_context[core] != this) {
        panic("In dht22::Driver destructor: At most one Driver per core is allowed.");
    }
    core_context[core] = nullptr;

    async_context_remove_when_pending_worker(context, &worker);

    const auto irq = which_dma_irq ? DMA_IRQ_1 : DMA_IRQ_0;
    const auto handler = which_dma_irq ? &dma_irq_handler<1> : &dma_irq_handler<0>;
    irq_remove_handler(irq, handler);
    if (!irq_previously_enabled) {
        irq_set_enabled(irq, false);
    }

    if (program_offsets[0] != -1) {
        pio_remove_program(pio0, &dht22_program, program_offsets[0]);
    }
    if (program_offsets[1] != -1) {
        pio_remove_program(pio1, &dht22_program, program_offsets[1]);
    }
}

inline
int Driver::load(PIO pio) {
    const int which = (pio == pio0 ? 0 : 1);
    if (program_offsets[which] != -1) {
        return program_offsets[which];
    }
    return program_offsets[which] = pio_add_program(pio, &dht22_program);
}

inline
Sensor::Sensor(Driver *driver, PIO pio, int gpio_pin)
: data()
, ready(false)
, driver(driver) {
    std::printf("dht22::Sensor::Sensor\n");
    const auto slot = std::find(driver->sensors.begin(), driver->sensors.end(), nullptr);
    if (slot == driver->sensors.end()) {
        panic("In dht22::Sensor constructor: dht22::Driver is already full of sensors.");
    }
    *slot = this;

    set_pio(pio);
    const int program_offset = driver->load(pio);

    const bool required = true;
    state_machine = pio_claim_unused_sm(pio, required);
    dma_channel = dma_claim_unused_channel(required);

    {
        dma_channel_config cfg = dma_channel_get_default_config(dma_channel);
        // We want the receive (rx) data, not the transmit (tx) data.
        const bool is_tx = false;
        channel_config_set_dreq(&cfg, pio_get_dreq(pio, state_machine, is_tx));
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
        channel_config_set_read_increment(&cfg, false);
        channel_config_set_write_increment(&cfg, true);
        const uint count = 5;
        const bool trigger = true;
        dma_channel_configure(dma_channel, &cfg, data.data(), &pio->rxf[state_machine], count, trigger);
        dma_irqn_set_channel_enabled(driver->which_dma_irq, dma_channel, true);
    }

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
}

inline
Sensor::~Sensor() {
    std::printf("dht22::Sensor::~Sensor\n");
    const auto slot = std::find(driver->sensors.begin(), driver->sensors.end(), this);
    std::printf("@ %s:%d\n", __FILE__, __LINE__);
    if (slot == driver->sensors.end()) {
        std::printf("@ %s:%d\n", __FILE__, __LINE__);
        panic("In dht22::Sensor destructor: dht22::Driver is missing me.");
    }
    std::printf("@ %s:%d\n", __FILE__, __LINE__);
    *slot = nullptr;
    std::printf("@ %s:%d\n", __FILE__, __LINE__);

    dma_irqn_set_channel_enabled(driver->which_dma_irq, dma_channel, false);
    std::printf("@ %s:%d\n", __FILE__, __LINE__);
    dma_channel_abort(dma_channel);
    std::printf("@ %s:%d\n", __FILE__, __LINE__);
    dma_channel_unclaim(dma_channel);

    std::printf("@ %s:%d\n", __FILE__, __LINE__);
    PIO pio = get_pio();
    std::printf("@ %s:%d\n", __FILE__, __LINE__);
    pio_sm_set_enabled(pio, state_machine, false);
    std::printf("@ %s:%d\n", __FILE__, __LINE__);
    pio_sm_unclaim(pio, state_machine);
    std::printf("@ %s:%d\n", __FILE__, __LINE__);
}

inline
PIO Sensor::get_pio() const {
    return which_pio ? pio1 : pio0;
}

inline
void Sensor::set_pio(PIO pio) {
    which_pio = (pio == pio1);
}

inline
Coroutine<int> Sensor::measure(float *celsius, float *humidity_percent) {
    // Push some counters to the state machine. It will put them in its x and y
    // registers. We pack them together as two 16-bit values in one 32-bit
    // word.
    // This also synchronizes us with the state machine.
    const uint32_t initial_request_cycles = 1000;
    const uint32_t bits_to_receive = 40;
    const uint32_t payload = (initial_request_cycles << 16) | bits_to_receive;

    pio_sm_put(get_pio(), state_machine, payload);

    struct Awaiter {
        Sensor *sensor;

        bool await_ready() {
            // If the transfer is done, then we don't need to suspend.
            // This assumes that the transfer has already begun.
            const bool already_done = !dma_channel_is_busy(sensor->dma_channel);
            // TODO: remove this logging
            if (already_done) {
                std::printf("Too slow!\n");
            }
            return already_done;
        }

        bool await_suspend(std::coroutine_handle<> continuation) {
            sensor->continuation = continuation;
            return true;
        }

        void await_resume() {}
    };

    std::printf("before inner await\n");
    co_await Awaiter{this};
    std::printf("after inner await\n");

    const uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (data[4] != checksum) {
        co_return -1; // error
    }

    *humidity_percent = decode_humidity(data[0], data[1]);
    *celsius = decode_temperature(data[2], data[3]);

    // Re-trigger the DMA channel.
    const bool trigger = true;
    dma_channel_set_write_addr(dma_channel, data.data(), trigger);
    co_return 0; // success
}

} // namespace dht22
} // namespace picoro
