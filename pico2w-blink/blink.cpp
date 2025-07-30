/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <pico/assert.h>
#include <pico/stdlib.h>

// Pico W devices use a GPIO on the WIFI chip for the LED,
// so when building for Pico W, CYW43_WL_GPIO_LED_PIN will be defined
#include <pico/cyw43_arch.h>

#include <stdio.h>
#include <tusb.h>

constexpr auto LED_DELAY_MS = 250;

// Turn the led on or off
void pico_set_led(bool led_on) {
  // Ask the wifi "driver" to set the GPIO on or off
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
}

unsigned long long frames_encountered = 0;

void on_frame(void *arg1, int arg2, size_t arg3, const uint8_t *arg4) {
  ++frames_encountered;
  printf("arg1: %p\narg2: %d\narg3: %zu\narg4: %p\n\n", arg1, arg2, arg3, arg4);
}

int main() {
  int rc;
  stdio_init_all();

  // Wait for the host to attach to the USB terminal (i.e. ttyACM0).
  // Give up after 10 seconds.
  for (int i = 0; i < 10 && !tud_cdc_connected(); ++i) {
    sleep_ms(1000);
  }
  if (tud_cdc_connected()) {
    printf("Thanks for coming along.\n");
  }

  rc = cyw43_arch_init();
  hard_assert(rc == PICO_OK);

  cyw43_arch_enable_sta_mode();

  /**
   * @brief Set the monitor mode of the CYW43 device.
   *
   * @param self  the driver state object. This should always be \c &cyw43_state
   * @param value The value to set monitor mode (1 for enabled, 0 for disabled).
   * @param cb    A callback function to handle monitor mode data.
   *             The callback should have the signature:
   *             `void (*cb)(void *, int, size_t, const uint8_t *)`
   *
   * @return 0 on success, an error code on failure.
   */
  printf("I'm about to set monitor mode on.\n");
  rc = cyw43_set_monitor_mode(&cyw43_state, 1, &on_frame);
  if (rc) {
    printf("error setting monitor mode: %d\n", rc);
  } else {
    printf("monitor mode engaged.\n");
  }

  bool disengaged = false;
  for (unsigned long long i = 0;; ++i) {
    pico_set_led(true);
    sleep_ms(LED_DELAY_MS);
    pico_set_led(false);
    sleep_ms(LED_DELAY_MS);
    // printf(".");
    // Undefined behavior is fun!
    printf("%llu\n", frames_encountered);
    
    if (!disengaged && frames_encountered > 16) {
      printf("I'm about to set monitor mode off.\n");
      rc = cyw43_set_monitor_mode(&cyw43_state, 0, &on_frame);
      if (rc) {
        printf("error setting monitor mode off: %d\n", rc);
      } else {
        disengaged = true;
        printf("monitor mode disengaged.\n");
      }
    }
  }
}
