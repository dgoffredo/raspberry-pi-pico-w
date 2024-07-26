#pragma once

/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 **/

#include <stdio.h>
#include <math.h>

#include <pico/stdlib.h>
#include <hardware/gpio.h>

namespace dht {

const uint DHT_PIN = 15;
const uint MAX_TIMINGS = 85;

struct Reading {
    float humidity;
    float celsius;
};

void read_from_dht(Reading *result);

inline
int demo() {
    gpio_init(DHT_PIN);
    for (;;) {
        Reading reading = {};
        read_from_dht(&reading);
        float fahrenheit = (reading.celsius * 9 / 5) + 32;
        printf("Humidity = %.1f%%, Temperature = %.1fC (%.1fF)\n",
               reading.humidity, reading.celsius, fahrenheit);

        sleep_ms(2000);
    }
}

inline
void read_from_dht(Reading *result) {
    int data[5] = {0, 0, 0, 0, 0};
    const auto checksum = [&]() -> int {
      return (data[0] + data[1] + data[2] + data[3]) & 0xFF;
    };
    uint last = 1;
    uint j = 0;

    gpio_set_dir(DHT_PIN, GPIO_OUT);
    gpio_put(DHT_PIN, 0);
    sleep_ms(20);
    gpio_set_dir(DHT_PIN, GPIO_IN);

    for (uint i = 0; i < MAX_TIMINGS; i++) {
        uint count = 0;
        while (gpio_get(DHT_PIN) == last) {
            count++;
            sleep_us(1);
            if (count == 255) break;
        }
        last = gpio_get(DHT_PIN);
        if (count == 255) break;

        if ((i >= 4) && (i % 2 == 0)) {
            data[j / 8] <<= 1;
            if (count > 16) data[j / 8] |= 1;
            j++;
        }
    }

    if (j >= 40 && data[4] == checksum()) {
        result->humidity = (float) ((data[0] << 8) + data[1]) / 10;
        if (result->humidity > 100) {
            result->humidity = data[0];
        }
        result->celsius = (float) (((data[2] & 0x7F) << 8) + data[3]) / 10;
        if (result->celsius > 125) {
            result->celsius = data[2];
        }
        if (data[2] & 0x80) {
            result->celsius = -result->celsius;
        }
    } else if (j < 40) {
        printf("Bad data: Expected 40 bits but received %d\n", j);
    } else {
        printf("Bad data: Received checksum %d that doesn't match calculated %d\n", data[4], checksum());
    }
}

} // namespace dht
