#include <picoro/coroutine.h>
#include <picoro/drivers/dht22.h>
#include <picoro/event_loop.h>
#include <picoro/sleep.h>
#include <picoro/tcp.h>

#include <pico/async_context_poll.h>
#include <pico/cyw43_arch.h>
#include <pico/stdio.h>

#include <tusb.h>

#include "secrets.h" // `wifi_password`

#include <cassert>
#include <chrono>

// Work around `-Werror=unused-variable` in release builds.
#define ASSERT(WHAT) \
  do { \
    assert(WHAT); \
    (void) sizeof(WHAT); \
  } while (false)

struct Measurement {
  int sequence_number = 0;
  float celsius = 0;
  float humidity_percent = 0;
  int timeouts = 0;
  int failed_checksums = 0;
};

struct {
  Measurement top;
  Measurement middle;
  Measurement bottom;
} most_recent;

int format_response(char (&buffer)[2048]) {
  return std::snprintf(buffer, sizeof buffer,
    "HTTP/1.1 200 OK\r\n"
    "Connection: close\r\n"
    "Content-Type: application/json\r\n"
    "\r\n"
    "{\"top\": {\"sequence_number\": %d, \"celsius\": %.1f, \"humidity_percent\": %.1f, \"timeouts\": %d, \"failed_checksums\": %d},"
    " \"middle\": {\"sequence_number\": %d, \"celsius\": %.1f, \"humidity_percent\": %.1f, \"timeouts\": %d, \"failed_checksums\": %d},"
    " \"bottom\": {\"sequence_number\": %d, \"celsius\": %.1f, \"humidity_percent\": %.1f, \"timeouts\": %d, \"failed_checksums\": %d}"
    "}",
    most_recent.top.sequence_number,
    most_recent.top.celsius,
    most_recent.top.humidity_percent,
    most_recent.top.timeouts,
    most_recent.top.failed_checksums,
    most_recent.middle.sequence_number,
    most_recent.middle.celsius,
    most_recent.middle.humidity_percent,
    most_recent.middle.timeouts,
    most_recent.middle.failed_checksums,
    most_recent.bottom.sequence_number,
    most_recent.bottom.celsius,
    most_recent.bottom.humidity_percent,
    most_recent.bottom.timeouts,
    most_recent.bottom.failed_checksums);
}

const char *pico_describe(int error) {
    switch (error) {
    case PICO_OK: return "[PICO_OK]";
    case PICO_ERROR_TIMEOUT: return "[PICO_ERROR_TIMEOUT]";
    case PICO_ERROR_GENERIC: return "[PICO_ERROR_GENERIC]";
    case PICO_ERROR_NO_DATA: return "[PICO_ERROR_NO_DATA]";
    case PICO_ERROR_NOT_PERMITTED: return "[PICO_ERROR_NOT_PERMITTED]";
    case PICO_ERROR_INVALID_ARG: return "[PICO_ERROR_INVALID_ARG]";
    case PICO_ERROR_IO: return "[PICO_ERROR_IO]";
    case PICO_ERROR_BADAUTH: return "[PICO_ERROR_BADAUTH]";
    case PICO_ERROR_CONNECT_FAILED: return "[PICO_ERROR_CONNECT_FAILED]";
    case PICO_ERROR_INSUFFICIENT_RESOURCES: return "[PICO_ERROR_INSUFFICIENT_RESOURCES]";
    }
    return "Unknown Pico error code";
}

picoro::Coroutine<void> blink(async_context_t *context, int times, std::chrono::milliseconds period) {
    const bool led_state = cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN);
    const auto delay = period / 2;
    for (int i = 0; i < times; ++i) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, !led_state);
        co_await picoro::sleep_for(context, delay);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
        co_await picoro::sleep_for(context, delay);
    }
}

picoro::Coroutine<void> wifi_connect(async_context_t *ctx, const char *SSID, const char *password) {
    struct LEDGuard {
        LEDGuard() { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true); }
        ~LEDGuard() { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false); }
    } led_guard;

    cyw43_arch_enable_sta_mode();
    const uint32_t ultra_performance_giga_chad_power_mode =
        cyw43_pm_value(CYW43_NO_POWERSAVE_MODE, 20, 1, 1, 1);
    cyw43_wifi_pm(&cyw43_state, ultra_performance_giga_chad_power_mode);
    // cyw43_wifi_pm(&cyw43_state, CYW43_PERFORMANCE_PM);

    std::printf("Connecting to WiFi...\n");
    int rc = cyw43_arch_wifi_connect_async(SSID, password, CYW43_AUTH_WPA2_AES_PSK);
    if (rc) {
        std::printf("Error connecting to wifi: %s.", pico_describe(rc));
        // Blink a few times to show that there's a problem.
        co_await blink(ctx, 10, std::chrono::milliseconds(250));
        co_return;
    }

    for (;;) {
        const int wifi_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (wifi_status == CYW43_LINK_UP) {
            break;
        } else if (wifi_status == CYW43_LINK_FAIL) {
            // If we failed, then reset the adapter, wait a few seconds, and try again.
            rc = cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
            co_await picoro::sleep_for(ctx, std::chrono::seconds(5));
            int rc = cyw43_arch_wifi_connect_async(SSID, password, CYW43_AUTH_WPA2_AES_PSK);
            if (rc) {
                std::printf("Error connecting to wifi: %s.", pico_describe(rc));
                // Blink a few times to show that there's a problem.
                co_await blink(ctx, 10, std::chrono::milliseconds(250));
                co_return;
            }
        } else if (wifi_status == CYW43_LINK_NONET) {
            // If there was no network, keep trying to connect.
            rc = cyw43_arch_wifi_connect_async(SSID, password, CYW43_AUTH_WPA2_AES_PSK);
            if (rc) {
                std::printf("Error connecting to wifi: %s.", pico_describe(rc));
                // Blink a few times to show that there's a problem.
                co_await blink(ctx, 10, std::chrono::milliseconds(250));
                co_return;
            }
        }
        co_await picoro::sleep_for(ctx, std::chrono::seconds(1));
    }

    std::printf("Connected to WiFi.\n");
}

picoro::Coroutine<void> handle_client(picoro::Connection conn) {
    char buffer[2048];
    // Read the request (ought to be less than 2K in size), and ignore it.
    auto [count, err] = co_await conn.recv(buffer, sizeof buffer);
    if (err) {
      co_return;
    }
    count = format_response(buffer);
    std::tie(count, err) = co_await conn.send(std::string_view(buffer, count));
    if (err) {
        std::printf("handle_client: Error on send: %s\n", picoro::lwip_describe(err));
    }
}

picoro::Coroutine<void> http_server(int port, int listen_backlog) {
    std::printf("Starting server at %s on port %d\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), port);
    auto [listener, err] = picoro::listen(port, listen_backlog);
    if (err) {
        std::printf("http_server: Error starting server: %s\n", picoro::lwip_describe(err));
        co_return;
    }

    for (;;) {
        auto [conn, err] = co_await listener.accept();
        if (err) {
            std::printf("http_server: Error accepting connection: %s\n", picoro::lwip_describe(err));
            continue;
        }
        handle_client(std::move(conn)).detach();
    }
}

picoro::Coroutine<void> networking(async_context_t *ctx) {
    co_await wifi_connect(ctx, "Annoying Saxophone", wifi_password);
    const int port = 80;
    const int listen_backlog = 1;
    co_await http_server(port, listen_backlog);
}

// Wait for the host to attach to the USB terminal (e.g. ttyACM0).
// Blink the onboard LED while we're waiting.
// Give up after the specified number of seconds.
picoro::Coroutine<void> wait_for_usb_debug_attach(async_context_t *context, std::chrono::seconds timeout) {
    const int iterations = timeout / std::chrono::seconds(1);
    for (int i = 0; i < iterations && !tud_cdc_connected(); ++i) {
        co_await picoro::sleep_for(context, std::chrono::milliseconds(1000));
    }

    if (tud_cdc_connected()) {
      co_await picoro::sleep_for(context, std::chrono::seconds(1));
      printf("Glad you could make it.\n");
    }
}

picoro::Coroutine<void> monitor_sensor(
    async_context_t *ctx,
    picoro::dht22::Driver *driver,
    PIO pio,
    uint8_t data_pin,
    uint8_t power_pin,
    Measurement *latest) {
  // Rather than connecting each sensor's power directly to 3.3V, I connect
  // each to its own GPIO pin. The GPIO pin can provide more than enough
  // current for the sensor, and can be set low at will to power cycle the
  // sensor, which tends to lock up after 10-30 minutes.
  gpio_init(power_pin);
  gpio_pull_down(power_pin); // already is by default, but let's make sure
  gpio_set_dir(power_pin, 1); // 1 means "write mode"
  gpio_put(power_pin, 1); // 1 means "high"

  using Sensor = picoro::dht22::Sensor;
  Sensor sensor(driver, pio, data_pin);
  for (;;) {
    co_await picoro::sleep_for(ctx, std::chrono::milliseconds(2000));
    float celsius, humidity_percent;
    Sensor::Result rc = co_await sensor.measure(&celsius, &humidity_percent);
    switch (rc) {
    case Sensor::OK:
      ++latest->sequence_number;
      latest->celsius = celsius;
      latest->humidity_percent = humidity_percent;
      std::printf("{"
        "\"celsius\": %.1f, "
        "\"humidity_percent\": %.1f"
      "}\n", celsius, humidity_percent);
      continue; // skips the reset below
    case Sensor::TIMEOUT:
      ++latest->timeouts;
      break;
    case Sensor::FAILED_CHECKSUM:
      ++latest->failed_checksums;
      break;
    }
    std::printf("{\"error\": \"%s\"}\n", Sensor::describe(rc));
    sensor.reset();
    // The sensor might have stopped responding. Power cycle the sensor.
    gpio_put(power_pin, 0);
    // I've chosen "3 seconds" arbitrarily. Probably a much shorter off time
    // would suffice. It's a pain to test, though, because it takes a while
    // for the sensor to lock up.
    co_await picoro::sleep_for(ctx, std::chrono::seconds(3));
    gpio_put(power_pin, 1);
  }
}

picoro::Coroutine<void> sensors_main(async_context_t *ctx, picoro::dht22::Driver *driver) {
  co_await wait_for_usb_debug_attach(ctx, std::chrono::seconds(3));

  struct {
    uint data_pin;
    uint power_pin;
    Measurement *data;
  } sensors[] = {
    {.data_pin = 16, .power_pin = 13, .data = &most_recent.top},
    {.data_pin = 15, .power_pin = 0, .data = &most_recent.middle},
    {.data_pin = 22, .power_pin = 6, .data = &most_recent.bottom}
  };

  for (const auto [data_pin, power_pin, data] : sensors) {
    monitor_sensor(ctx, driver, pio0, data_pin, power_pin, data).detach();
  }
}

int main() {
    stdio_init_all();

    async_context_poll_t context;
    const bool ok = async_context_poll_init_with_defaults(&context);
    ASSERT(ok);
    async_context_t *const ctx = &context.core;

    // Do some WiFi chip setup here, just so that we can use the LED
    // immediately. The rest of the setup happens in `networking`.
    cyw43_arch_set_async_context(ctx);
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) { // 🇺🇸 🦅
        std::printf("failed to initialize WiFi\n");
        return 1;
    }

    constexpr int which_dma_irq = 0;
    picoro::dht22::Driver driver(ctx, which_dma_irq);

    picoro::run_event_loop(ctx,
        sensors_main(ctx, &driver),
        networking(ctx));

    // unreachable
    async_context_deinit(ctx);

    // unreachable
    cyw43_arch_deinit();
    async_context_deinit(ctx);
}
