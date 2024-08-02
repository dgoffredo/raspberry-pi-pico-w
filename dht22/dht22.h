#pragma once

/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 **/

#include <stdio.h>
#include <math.h>

#include <chrono>

#include <pico/async_context.h>
#include <pico/stdlib.h>
#include <hardware/gpio.h>

#include <picoro/coroutine.h>
#include <picoro/sleep.h>

namespace dht {

const uint MAX_TIMINGS = 85;

struct Reading {
    float humidity;
    float celsius;
};

void read_from_dht(uint gpio_pin, Reading *result);

struct Sensor {
    uint gpio_pin : 5;
    bool pull_up;
};

inline
picoro::Coroutine<void> demo(async_context_t *context) {
    const Sensor sensors[] = {
        {.gpio_pin=15, .pull_up=true},
        {.gpio_pin=16, .pull_up=true},
        {.gpio_pin=22, .pull_up=true},
    };
    for (const Sensor& sensor : sensors) {
      gpio_init(sensor.gpio_pin);
      if (sensor.pull_up) {
          gpio_pull_up(sensor.gpio_pin);
      }
    }
    for (;;) {
        for (const Sensor& sensor : sensors) {
            Reading reading = {};
            // printf("pin %u\n", sensor.gpio_pin);
            read_from_dht(sensor.gpio_pin, &reading);
            float fahrenheit = (reading.celsius * 9 / 5) + 32;
            // printf("\tHumidity = %.1f%%, Temperature = %.1fC (%.1fF)\n",
            //    reading.humidity, reading.celsius, fahrenheit);
            printf("{\"pin\": %u, \"humidity_percent\": %.1f, \"celsius\": %.1f, \"fahrenheit\": %.1f}\n",
               sensor.gpio_pin, reading.humidity, reading.celsius, fahrenheit);
        }
        co_await picoro::sleep_for(context, std::chrono::seconds(2));
    }
}

inline
void read_from_dht(uint gpio_pin, Reading *result) {
    int data[5] = {0, 0, 0, 0, 0};
    const auto checksum = [&]() -> int {
      return (data[0] + data[1] + data[2] + data[3]) & 0xFF;
    };
    uint last = 1;
    uint current;
    uint j = 0;

    char debug_buffer[4096] = {};
    const char *const dbg_end = debug_buffer + sizeof debug_buffer;
    char *dbg = debug_buffer; // pointer into `debug_buffer`
    const auto dprintf = [&](const char *format, auto... args) -> int {
      const int count = snprintf(dbg, dbg_end - dbg, format, args...);
      dbg += count;
      return count;
    };
    dprintf("--"); // pin starts high

    gpio_set_dir(gpio_pin, GPIO_OUT);
    gpio_put(gpio_pin, 0);
    sleep_ms(20);
    gpio_set_dir(gpio_pin, GPIO_IN);

    for (uint i = 0; i < MAX_TIMINGS; i++) {
        uint count = 0;
        while ((current = gpio_get(gpio_pin)) == last) {
            count++;
            sleep_us(1);
            if (count == 255) break;
        }
        dprintf("%u %s", count, last ? "--" : "__");
        last = current;
        if (count == 255) break;

        if ((i >= 4) && (i % 2 == 0)) {
            data[j / 8] <<= 1;
            if (count > 16) data[j / 8] |= 1;
            j++;
        }
    }

    /*printf("\tData is 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
      (unsigned)data[0],
      (unsigned)data[1],
      (unsigned)data[2],
      (unsigned)data[3],
      (unsigned)data[4]);*/

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
        // printf("\tBad data: Expected 40 bits but received %d\n", j);
    } else {
        // printf("\tBad data: Received checksum %d that doesn't match calculated %d\n", data[4], checksum());
    }

    // printf("\tsignal history: %s\n", debug_buffer);
}

} // namespace dht
