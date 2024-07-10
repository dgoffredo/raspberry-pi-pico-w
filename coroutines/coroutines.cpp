#include "pico/async_context_poll.h"
#include "pico/binary_info.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <tusb.h>

#include <stdio.h>
#include <chrono>
#include <cmath>

#include "picoro.h"
#include "scd4x.h"

// Wait for the host to attach to the USB terminal (i.e. ttyACM0).
// Blink the onboard LED while we're waiting.
// Give up after the specified number of seconds.
picoro::Coroutine<void> wait_for_usb_debug_attach(async_context_t *context, std::chrono::seconds timeout) {
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

picoro::Coroutine<bool> data_ready(const sensirion::SCD4x& sensor) {
    bool result;
    int rc = co_await sensor.get_data_ready_flag(&result);
    if (rc) {
        printf("Unable to query whether the sensor has data ready. Error code %d.\n", rc);
        co_return false;
    }
    co_return result;
}

int print_millis_as_decimal(int32_t millis) {
    // 23456 → "23.456"
    // 23 →  "0.023"
    // 1000 → "1.000"
    return printf("%ld.%03ld", millis / 1000, std::abs(millis) % 1000);
}

picoro::Coroutine<void> monitor_scd4x(async_context_t *context) {
    // I²C GPIO pins
    const uint sda_pin = 12;
    const uint scl_pin = 13;
    // I²C clock rate
    const uint clock_hz = 400 * 1000;

    i2c_inst_t *const instance = i2c0;
    const uint actual_baudrate = i2c_init(instance, clock_hz);
    printf("The actual I2C baudrate is %u Hz\n", actual_baudrate);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);

    sensirion::SCD4x sensor{context};
    sensor.device.instance = instance;

    int rc = co_await sensor.set_automatic_self_calibration(0);
    if (rc) {
        printf("Unable to disable automatic self-calibration. Error code %d.\n", rc);
    }

    rc = co_await sensor.start_periodic_measurement();
    if (rc) {
        printf("Unable to start periodic measurement mode. Error code %d.\n", rc);
    } 

    for (;;) {
        co_await picoro::sleep_for(context, std::chrono::seconds(5));
        while (! co_await data_ready(sensor)) {
            co_await picoro::sleep_for(context, std::chrono::seconds(1));
        }
        
        uint16_t co2_ppm;
        int32_t temperature_millicelsius;
        int32_t relative_humidity_millipercent;
        rc = co_await sensor.read_measurement(&co2_ppm, &temperature_millicelsius, &relative_humidity_millipercent);
        if (rc) {
            printf("Unable to read sensor measurement. Error code %d.\n", rc);
        } else {
            printf("CO2: %u ppm\ttemperature: ", (unsigned)co2_ppm);
            print_millis_as_decimal(temperature_millicelsius);
            printf(" C\thumidity: ");
            print_millis_as_decimal(relative_humidity_millipercent);
            printf("%%\n");
        }
    } 
}

picoro::Coroutine<void> coroutine_main(async_context_t *context) {
    co_await wait_for_usb_debug_attach(context, std::chrono::seconds(10));
    co_await monitor_scd4x(context);
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
