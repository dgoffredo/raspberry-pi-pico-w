// This file is written in C11 (ISO/IEC 9899:2011).

#include "hardware/i2c.h"
#include "pico/binary_info.h"
#include "pico/stdlib.h"

#include "scd4x_i2c.h"
#include <stdio.h>

// I2C GPIO pins
static const uint sda_pin = 12;
static const uint scl_pin = 13;

// I2C clock rate
static const uint clock_hz = 400 * 1000;

int main(void) {
    stdio_init_all();

    i2c_init(i2c_default, clock_hz);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);

    int status = 0;

    // Stop any readings if occuring
    status = scd4x_stop_periodic_measurement();
    printf("scd4x_stop_periodic_measurement() returned status %d\n", status);

    // Perform self test
    uint16_t* selfTest = 0;
    status = scd4x_perform_self_test(selfTest);
    printf("scd4x_perform_self_test returned status %d\n", status);

    // Get Serial number 3 parts
    uint16_t one;
    uint16_t two;
    uint16_t three;
    status = scd4x_get_serial_number(&one, &two, &three);
    printf("scd4x_get_serial_number returned status %d\n", status);

    status = scd4x_start_periodic_measurement();
    printf("scd4x_start_periodic_measurement() returned status %d\n", status);

    for (;;) {
        bool dataReady = false;
        for (;;) {
            status = scd4x_get_data_ready_flag(&dataReady);
            printf("scd4x_get_data_ready_flag returned status %d\n", status);
            if (dataReady) {
                break;
            }
            sleep_ms(1000);
        }

        uint16_t co2_raw = 0;
        uint16_t temp_raw = 0;
        uint16_t humidity_raw = 0;
        status = scd4x_read_measurement_ticks(&co2_raw, &temp_raw, &humidity_raw);
        printf("scd4x_read_measurement_ticks returned status %d\n", status);

        const int tempInCelsius = -45 + 175 * temp_raw / 65536;
        const int humidityPercent = 100 * humidity_raw / 65536;

        printf("CO2: %d ppm, Temperature: %d C, Humidity: %d%%\n", co2_raw, tempInCelsius, humidityPercent);
        sleep_ms(5000);
    }
}
