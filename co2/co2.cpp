#include "hardware/i2c.h"
#include "pico/binary_info.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <tusb.h>

#include "scd4x_i2c.h"
#include <stdio.h>

namespace {

// I2C GPIO pins
const uint sda_pin = 12;
const uint scl_pin = 13;

// I2C clock rate
const uint clock_hz = 400 * 1000;

} // namespace

#define CHECKPOINT() printf("[%s:%d]\n", __FILE__, __LINE__)

int main() {
    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    // Wait for the host to attach to the USB terminal (i.e. ttyACM0).
    while (!tud_cdc_connected()) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(500);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(500);
    }
    
    const uint actual_baudrate = i2c_init(i2c_default, clock_hz);
    printf("The actual I2C baudrate is %u Hz\n", actual_baudrate);
    CHECKPOINT();
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    CHECKPOINT();
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    CHECKPOINT();
    gpio_pull_up(sda_pin);
    CHECKPOINT();
    gpio_pull_up(scl_pin);
    CHECKPOINT();

    int status = 0;

    // Stop any readings if occurring
    /*
    status = scd4x_stop_periodic_measurement();
    CHECKPOINT();
    printf("scd4x_stop_periodic_measurement() returned status %d\n", status);
    CHECKPOINT();
    */

    /*
    // Perform self test
    uint16_t selfTest;
    status = scd4x_perform_self_test(&selfTest);
    CHECKPOINT();
    printf("scd4x_perform_self_test returned status %d\n", status);
    CHECKPOINT();
    if (!status) {
        printf("the result of the self test is: %u\n", selfTest);
    }
    */

    // Get Serial number 3 parts
    /*
    uint16_t one;
    uint16_t two;
    uint16_t three;
    status = scd4x_get_serial_number(&one, &two, &three);
    CHECKPOINT();
    printf("scd4x_get_serial_number returned status %d\n", status);
    if (!status) {
        printf("Sensor serial number is: 0x%x 0x%x 0x%x\n", (int)one, (int)two, (int)three);
    }
    CHECKPOINT();
    */

    status = scd4x_start_periodic_measurement();
    CHECKPOINT();
    printf("scd4x_start_periodic_measurement() returned status %d\n", status);

    for (;;) {
        bool dataReady = false;
        for (;;) {
            CHECKPOINT();
            status = scd4x_get_data_ready_flag(&dataReady);
            printf("scd4x_get_data_ready_flag returned status %d\n", status);
            if (dataReady) {
                break;
            }
            sleep_ms(1000);
        }

        CHECKPOINT();
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
