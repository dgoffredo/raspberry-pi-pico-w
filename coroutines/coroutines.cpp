#include "pico/async_context_poll.h"
#include "pico/binary_info.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <tusb.h>

#include <malloc.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cmath>
#include <cstdio>
#include <iterator>

#include <hardware/watchdog.h>
#include <picoro/coroutine.h>
#include <picoro/debug.h>
#include <picoro/event_loop.h>
#include <picoro/sleep.h>
#include <picoro/tcp.h>
#include <picoro/drivers/scd4x.h>

#include "secrets.h"

const char *cyw43_describe(int status) {
    switch (status) {
    case CYW43_LINK_DOWN: return "[CYW43_LINK_DOWN] Wifi down";
    case CYW43_LINK_JOIN: return "[CYW43_LINK_JOIN] Connected to wifi";
    case CYW43_LINK_NOIP: return "[CYW43_LINK_NOIP] Connected to wifi, but no IP address";
    case CYW43_LINK_UP: return "[CYW43_LINK_UP] Connected to wifi with an IP address";
    case CYW43_LINK_FAIL: return "[CYW43_LINK_FAIL] Connection failed";
    case CYW43_LINK_NONET: return "[CYW43_LINK_NONET] No matching SSID found (could be out of range, or down)";
    case CYW43_LINK_BADAUTH: return "[CYW43_LINK_BADAUTH] Authentication failure";
    }
    return "Unknown cyw43 status code";
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

struct Measurement {
    unsigned sequence_number = 0;
    uint16_t co2_ppm = 0;
    int32_t temperature_millicelsius = 0;
    int32_t relative_humidity_millipercent = 0;
} latest;

// TODO: Need to recalculate this. For now I fudge it up to 511.
constexpr std::size_t max_response_length = 511;

uint32_t get_total_heap() {
   extern char __StackLimit, __bss_end__;
   return &__StackLimit  - &__bss_end__;
}

uint32_t get_free_heap() {
   struct mallinfo const m = mallinfo();
   return get_total_heap() - m.uordblks;
}

int format_response(
    // +1 for the null terminator
    std::array<char, max_response_length + 1>& buffer,
    const Measurement& data) {

    // If we haven't made a measurement yet, respond with an error telling the
    // client to try again in a few seconds.
    if (data.sequence_number == 0) {
        constexpr char response[] =
            "HTTP/1.1 503 Service Unavailable\r\n"
            "Connection: close\r\n"
            "Retry-After: 5\r\n"
            "\r\n";
        return std::snprintf(buffer.data(), buffer.size(), "%s", response);
    }

    constexpr char response_format[] =
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "Content-Type: application/json\r\n"
        "\r\n"
        "{\"sequence_number\": %u,"
        " \"CO2_ppm\": %hu,"
        " \"temperature_celsius\": %.1f,"
        " \"relative_humidity_percent\": %.1f,"
        " \"free_bytes\": %lu}";

    const uint32_t free_bytes = get_free_heap();

    return std::snprintf(
        buffer.data(),
        buffer.size(),
        response_format,
        data.sequence_number,
        data.co2_ppm,
        data.temperature_millicelsius / 1000.0f,
        data.relative_humidity_millipercent / 1000.0f,
        free_bytes);
}

// Wait for the host to attach to the USB terminal (i.e. ttyACM0).
// Blink the onboard LED while we're waiting.
// Give up after the specified number of seconds.
picoro::Coroutine<void> wait_for_usb_debug_attach(async_context_t *ctx, std::chrono::seconds timeout) {
    const int iterations = timeout / std::chrono::seconds(1);
    for (int i = 0; i < iterations && !tud_cdc_connected(); ++i) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        co_await picoro::sleep_for(ctx, std::chrono::milliseconds(500));
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        co_await picoro::sleep_for(ctx, std::chrono::milliseconds(500));
    }

    co_await picoro::sleep_for(ctx, std::chrono::seconds(1));
    std::printf("Glad you could make it.\n");
}

picoro::Coroutine<bool> data_ready(const sensirion::SCD4x& sensor) {
    bool result;
    int rc = co_await sensor.get_data_ready_flag(&result);
    if (rc) {
        debug("Unable to query whether the sensor has data ready. Error code %d.\n", rc);
        co_return false;
    }
    co_return result;
}

picoro::Coroutine<void> monitor_scd4x(async_context_t *ctx) {
    // I²C GPIO pins
    const uint sda_pin = 12;
    const uint scl_pin = 13;
    // I²C clock rate
    const uint clock_hz = 400 * 1000;

    i2c_inst_t *const instance = i2c0;
    const uint actual_baudrate = i2c_init(instance, clock_hz);
    std::printf("The actual I2C baudrate is %u Hz\n", actual_baudrate);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);

    sensirion::SCD4x sensor{ctx};
    sensor.device.instance = instance;

    int rc = co_await sensor.set_automatic_self_calibration(0);
    if (rc) {
        debug("Unable to disable automatic self-calibration. Error code %d.\n", rc);
    }

    rc = co_await sensor.start_periodic_measurement();
    if (rc) {
        debug("Unable to start periodic measurement mode. Error code %d.\n", rc);
    }

    for (;;) {
        co_await picoro::sleep_for(ctx, std::chrono::seconds(5));
        while (! co_await data_ready(sensor)) {
            co_await picoro::sleep_for(ctx, std::chrono::seconds(1));
        }

        uint16_t co2_ppm;
        int32_t temperature_millicelsius;
        int32_t relative_humidity_millipercent;
        rc = co_await sensor.read_measurement(&co2_ppm, &temperature_millicelsius, &relative_humidity_millipercent);
        if (rc) {
            debug("Unable to read sensor measurement. Error code %d.\n", rc);
        } else {
            std::printf("CO2: %hu ppm\ttemperature: %.1f C\thumidity: %.1f%%\n", co2_ppm, temperature_millicelsius / 1000.0f, relative_humidity_millipercent / 1000.0f);
            ++latest.sequence_number;
            latest.co2_ppm = co2_ppm;
            latest.temperature_millicelsius = temperature_millicelsius;
            latest.relative_humidity_millipercent = relative_humidity_millipercent;
        }
    }

    // unreachable
    i2c_deinit(instance);
}

picoro::Coroutine<void> blink(async_context_t *ctx, int times, std::chrono::milliseconds period) {
    const bool led_state = cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN);
    const auto delay = period / 2;
    for (int i = 0; i < times; ++i) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, !led_state);
        co_await picoro::sleep_for(ctx, delay);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
        co_await picoro::sleep_for(ctx, delay);
    }
}

picoro::Coroutine<void> wifi_connect(async_context_t *ctx, const char *SSID, const char *password) {
    struct LEDGuard {
        LEDGuard() { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true); }
        ~LEDGuard() { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false); }
    } led_guard;

    cyw43_arch_enable_sta_mode();
    // const uint32_t ultra_performance_giga_chad_power_mode =
    //     cyw43_pm_value(CYW43_NO_POWERSAVE_MODE, 20, 1, 1, 1);
    // cyw43_wifi_pm(&cyw43_state, ultra_performance_giga_chad_power_mode);
    // cyw43_wifi_pm(&cyw43_state, CYW43_PERFORMANCE_PM);
    cyw43_wifi_pm(&cyw43_state, CYW43_AGGRESSIVE_PM);

    debug("Connecting to WiFi...\n");
    int rc = cyw43_arch_wifi_connect_async(SSID, password, CYW43_AUTH_WPA2_AES_PSK);
    if (rc) {
        debug("Error connecting to wifi: %s.", pico_describe(rc));
        // Blink a few times to show that there's a problem.
        co_await blink(ctx, 10, std::chrono::milliseconds(250));
        co_return;
    }

    for (;;) {
        const int wifi_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        debug("WiFi status: %s\n", cyw43_describe(wifi_status));
        if (wifi_status == CYW43_LINK_UP) {
            break;
        } else if (wifi_status == CYW43_LINK_FAIL) {
            // If we failed, then reset the adapter, wait a few seconds, and try again.
            rc = cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
            debug("Disassociating from WiFi network. rcode: %d\n", rc);
            debug("Will retry WiFi in a few seconds.\n");
            co_await picoro::sleep_for(ctx, std::chrono::seconds(5));
            debug("Connecting to WiFi...\n");
            int rc = cyw43_arch_wifi_connect_async(SSID, password, CYW43_AUTH_WPA2_AES_PSK);
            if (rc) {
                debug("Error connecting to wifi: %s.", pico_describe(rc));
                // Blink a few times to show that there's a problem.
                co_await blink(ctx, 10, std::chrono::milliseconds(250));
                co_return;
            }
        } else if (wifi_status == CYW43_LINK_NONET) {
            // If there was no network, keep trying to connect.
            rc = cyw43_arch_wifi_connect_async(SSID, password, CYW43_AUTH_WPA2_AES_PSK);
            if (rc) {
                debug("Error connecting to wifi: %s.", pico_describe(rc));
                // Blink a few times to show that there's a problem.
                co_await blink(ctx, 10, std::chrono::milliseconds(250));
                co_return;
            }
        }
        co_await picoro::sleep_for(ctx, std::chrono::seconds(1));
    }

    debug("Connected to WiFi.\n");
}

picoro::Coroutine<void> handle_client(picoro::Connection conn) {
    debug("in handle_client(...), about to await recv()\n");
    char readbuf[2048];
    auto [count, err] = co_await conn.recv(readbuf, sizeof readbuf);
    debug("in handle_client(...), received %d bytes with error %s\n", count, picoro::lwip_describe(err));
    if (err) {
      debug("in handle_client(...), since there was an error, I'm closing the connection and returning.\n");
      co_return;
    }
    std::array<char, max_response_length + 1> buffer;
    debug("in handle_client(...), about to format response and await send()\n");
    count = format_response(buffer, latest);
    std::tie(count, err) = co_await conn.send(std::string_view(buffer.data(), count));
    debug("handle_client(...), finished send(). Sent %d bytes with error %s. About to close and return.\n", count, picoro::lwip_describe(err));
}

picoro::Coroutine<void> http_server(int port, int listen_backlog) {
    debug("http_server: Starting server at %s on port %d\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), port);
    auto [listener, err] = picoro::listen(port, listen_backlog);
    if (err) {
        debug("http_server: Error starting server: %s\n", picoro::lwip_describe(err));
        co_return;
    }
    debug("http_server: server started\n");

    for (;;) {
        debug("http_server: about to await accept()\n");
        auto [conn, err] = co_await listener.accept();
        if (err) {
            debug("http_server: Error accepting connection: %s\n", picoro::lwip_describe(err));
            continue;
        }
        debug("http_server: accept()ed a connection\n");
        handle_client(std::move(conn)).detach();
    }
}

picoro::Coroutine<void> networking(async_context_t *ctx) {
    co_await wifi_connect(ctx, "Annoying Saxophone", wifi_password);
    const int port = 80;
    const int listen_backlog = 1;
    co_await http_server(port, listen_backlog);
}

picoro::Coroutine<void> coroutine_main(async_context_t *ctx) {
    co_await wait_for_usb_debug_attach(ctx, std::chrono::seconds(10));
    // Run the WiFi and server setup in the background.
    auto server = networking(ctx);
    // Loop forever reading sensor data.
    co_await monitor_scd4x(ctx);
}

picoro::Coroutine<void> time_beacon(async_context_t *ctx) {
    for (;;) {
        watchdog_update();
        const uint64_t microseconds = to_us_since_boot(get_absolute_time());
        debug("%" PRIu64 " microseconds since boot.", microseconds);
        co_await picoro::sleep_for(ctx, std::chrono::seconds(1));
    }
}

int main() {
    stdio_init_all();

    if (watchdog_caused_reboot()) {
        printf("rebooted by watchdog\n");
    }
    // Watchdog is updated every second in `time_beacon`. If we miss five
    // updates, then watchdog will reset the board.
    const uint32_t timeout_ms = 5000;
    const bool pause_on_debug = true;
    watchdog_enable(timeout_ms, pause_on_debug);

    async_context_poll_t context = {};
    bool succeeded = async_context_poll_init_with_defaults(&context);
    if (!succeeded) {
        debug("Failed to initialize async_context_poll_t\n");
        return -2;
    }
    async_context_t *const ctx = &context.core;

    // Do some WiFi chip setup here, just so that we can use the LED
    // immediately. The rest of the setup happens in `coroutine_main`.
    cyw43_arch_set_async_context(ctx);
    // 🇺🇸 🦅
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        debug("failed to initialize WiFi\n");
        return 1;
    }

    run_event_loop(ctx, coroutine_main(ctx), time_beacon(ctx));

    // unreachable
    cyw43_arch_deinit();
    async_context_deinit(ctx);
}
