#include "pico/async_context_poll.h"
#include "pico/binary_info.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <tusb.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cmath>
#include <cstdio>

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "picoro.h"
#include "scd4x.h"
#include "secrets.h"

#define debug(...) printf(__VA_ARGS__)

const char *wifi_describe(err_t error) {
    switch (error) {
    case ERR_OK: return "[ERR_OK] No error, everything OK";
    case ERR_MEM: return "[ERR_MEM] Out of memory error";
    case ERR_BUF: return "[ERR_BUF] Buffer error";
    case ERR_TIMEOUT: return "[ERR_TIMEOUT] Timeout";
    case ERR_RTE: return "[ERR_RTE] Routing problem";
    case ERR_INPROGRESS: return "[ERR_INPROGRESS] Operation in progress";
    case ERR_VAL: return "[ERR_VAL] Illegal value";
    case ERR_WOULDBLOCK: return "[ERR_WOULDBLOCK] Operation would block";
    case ERR_USE: return "[ERR_USE] Address in use";
    case ERR_ALREADY: return "[ERR_ALREADY] Already connecting";
    case ERR_ISCONN: return "[ERR_ISCONN] Conn already established";
    case ERR_CONN: return "[ERR_CONN] Not connected";
    case ERR_IF: return "[ERR_IF] Low-level netif error";
    case ERR_ABRT: return "[ERR_ABRT] Connection aborted";
    case ERR_RST: return "[ERR_RST] Connection reset";
    case ERR_CLSD: return "[ERR_CLSD] Connection closed";
    case ERR_ARG: return "[ERR_ARG] Illegal argument";
    }
    return "Unknown lwIP error code";
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

// 225 bytes is the largest the response could ever be. I counted.
constexpr std::size_t max_response_length = 225;

int format_response(
    // +1 for the null terminator
    std::array<char, max_response_length + 1>& buffer,
    const Measurement& data) {

    constexpr char response_format[] =
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "Content-Type: application/json\r\n"
        "\r\n"
        "{\"sequence_number\": %u,"
        " \"CO2_ppm\": %hu,"
        " \"temperature_celsius\": %ld.%03ld,"
        " \"relative_humidity_percent\": %ld.%03ld}";
    
    return std::snprintf(
        buffer.data(),
        buffer.size(),
        response_format,
        data.sequence_number,
        data.co2_ppm,
        data.temperature_millicelsius / 1000,
        std::abs(data.temperature_millicelsius) % 1000,
        data.relative_humidity_millipercent / 1000,
        std::abs(data.relative_humidity_millipercent) / 1000);
}

struct Client {
    int response_length = 0;
    int response_bytes_acked = 0;
    // +1 for the null terminator
    std::array<char, max_response_length + 1> response_buffer;
};

err_t send_response(Client& client, tcp_pcb *client_pcb) {
    client.response_length = format_response(client.response_buffer, latest);
    debug("Sending response of length %d\n", client.response_length);

    const u8_t flags = 0;
    err_t err = tcp_write(client_pcb, client.response_buffer.data(), client.response_length, flags);
    if (err) {
        debug("tcp_write error: %s\n", wifi_describe(err));
        return err;
    }

    err = tcp_output(client_pcb); 	
    if (err) {
        debug("tcp_output error: %s\n", wifi_describe(err));
    }

    return err;
}

err_t cleanup_connection(Client *client, tcp_pcb *client_pcb) {
    tcp_arg(client_pcb, NULL);
    tcp_sent(client_pcb, NULL);
    tcp_recv(client_pcb, NULL);
    tcp_err(client_pcb, NULL);

    delete client;

    err_t err = tcp_close(client_pcb);
    if (err) {
        debug("tcp_close error: %s\n", wifi_describe(err));
    }
    return err;
}

err_t on_sent(void *arg, tcp_pcb *client_pcb, u16_t len) {
    auto *client = static_cast<Client*>(arg);
    client->response_bytes_acked += len;
    debug("client ACK'd %d/%d bytes of the sent response\n", client->response_bytes_acked, client->response_length);
    if (client->response_bytes_acked == client->response_length) {
        debug("Client has received the entire response. Closing connection.\n");
        return cleanup_connection(client, client_pcb);
    }
    return ERR_OK;
}

err_t on_recv(void *arg, tcp_pcb *client_pcb, pbuf *buf, err_t err) {
    if (err) {
        debug("on_recv error: %s\n", wifi_describe(err));
        if (buf) {
            pbuf_free(buf);
        }
        return ERR_OK;
    }

    if (!buf) {
        debug("The client closed their side of the connection. Closing our side now.\n");
        return cleanup_connection(static_cast<Client*>(arg), client_pcb);
    }

    if (buf->tot_len > 0) {
        debug("tcp_server_recv %d bytes. status: %s\n", buf->tot_len, wifi_describe(err));
        tcp_recved(client_pcb, buf->tot_len);
    }
    pbuf_free(buf);

    return ERR_OK;
}

void on_err(void *arg, err_t err) {
    debug("Connection fatal error: %s\n", wifi_describe(err));
    delete static_cast<Client*>(arg);
}

err_t on_accept(void *arg, tcp_pcb *client_pcb, err_t err) {
    if (err || client_pcb == NULL) {
        debug("Failed to accept a connection: %s\n", wifi_describe(err));
        return ERR_VAL; // Is this the appropriate return value?
    }
    debug("Client connected.\n");

    auto *client = new Client;
    tcp_arg(client_pcb, client);
    tcp_sent(client_pcb, on_sent);
    tcp_recv(client_pcb, on_recv);
    tcp_err(client_pcb, on_err);

    return send_response(*client, client_pcb);
}

void setup_http_server(u16_t port) {
    debug("Starting server at %s on port %u\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), port);

    tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        debug("Failed to create server PCB.\n");
        return;
    }

    err_t err = tcp_bind(pcb, IP_ADDR_ANY, port);
    if (err) {
        debug("Failed to bind to port %u: %s\n", port, wifi_describe(err));
        return;
    }

    const u8_t backlog = 1;
    pcb = tcp_listen_with_backlog_and_err(pcb, backlog, &err);
    if (!pcb) {
        debug("Failed to listen: %s\n", wifi_describe(err));
        return;
    }

    tcp_accept(pcb, on_accept);
}

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
        debug("Unable to query whether the sensor has data ready. Error code %d.\n", rc);
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
        debug("Unable to disable automatic self-calibration. Error code %d.\n", rc);
    }

    rc = co_await sensor.start_periodic_measurement();
    if (rc) {
        debug("Unable to start periodic measurement mode. Error code %d.\n", rc);
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
            debug("Unable to read sensor measurement. Error code %d.\n", rc);
        } else {
            printf("CO2: %hu ppm\ttemperature: ", co2_ppm);
            print_millis_as_decimal(temperature_millicelsius);
            printf(" C\thumidity: ");
            print_millis_as_decimal(relative_humidity_millipercent);
            printf("%%\n");

            ++latest.sequence_number;
            latest.co2_ppm = co2_ppm;
            latest.temperature_millicelsius = temperature_millicelsius;
            latest.relative_humidity_millipercent = relative_humidity_millipercent;
        }
    }

    // unreachable
    i2c_deinit(instance);
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

picoro::Coroutine<void> wifi_connect(async_context_t *context, const char *SSID, const char *password) {
    struct LEDGuard {
        LEDGuard() { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true); }
        ~LEDGuard() { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false); }
    } led_guard;

    cyw43_arch_enable_sta_mode();
    // const uint32_t ultra_performance_giga_chad_power_mode =
    //     cyw43_pm_value(CYW43_NO_POWERSAVE_MODE, 20, 1, 1, 1);
    // cyw43_wifi_pm(&cyw43_state, ultra_performance_giga_chad_power_mode);
    cyw43_wifi_pm(&cyw43_state, CYW43_PERFORMANCE_PM);

    debug("Connecting to WiFi...\n");
    int rc = cyw43_arch_wifi_connect_async(SSID, password, CYW43_AUTH_WPA2_AES_PSK);
    if (rc) {
        debug("Error connecting to wifi: %s.", pico_describe(rc));
        // Blink a few times to show that there's a problem.
        co_await blink(context, 10, std::chrono::milliseconds(250));
        co_return;
    }

    // This loop is based on the source code of
    // `cyw43_arch_wifi_connect_bssid_until` from the SDK.
    int wifi_status;
    for (;;) {
        wifi_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (wifi_status == CYW43_LINK_UP) {
            break;
        }
        // If there was no network, keep trying to connect.
        if (wifi_status == CYW43_LINK_NONET) {
            rc = cyw43_arch_wifi_connect_async(SSID, password, CYW43_AUTH_WPA2_AES_PSK);
            if (rc) {
                debug("Error connecting to wifi: %s.", pico_describe(rc));
                // Blink a few times to show that there's a problem.
                co_await blink(context, 10, std::chrono::milliseconds(250));
                co_return;
            }
        }
        co_await picoro::sleep_for(context, std::chrono::seconds(1));
    }

    debug("Connected to WiFi.\n");
}

picoro::Coroutine<void> networking(async_context_t *context) {
    co_await wifi_connect(context, "Annoying Saxophone", wifi_password);
    setup_http_server(80);
}

picoro::Coroutine<void> coroutine_main(async_context_t *context) {
    co_await wait_for_usb_debug_attach(context, std::chrono::seconds(10));
    // Run the WiFi and server setup in the background.
    auto server = networking(context);
    // Loop forever reading sensor data.
    co_await monitor_scd4x(context);
}

int main() {
    stdio_init_all();

    async_context_poll_t context = {};
    bool succeeded = async_context_poll_init_with_defaults(&context);
    if (!succeeded) {
        debug("Failed to initialize async_context_poll_t\n");
        return -2;
    }

    // Do some WiFi chip setup here, just so that we can use the LED
    // immediately. The rest of the setup happens in `coroutine_main`.
    cyw43_arch_set_async_context(&context.core);
    // 🇺🇸 🦅
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        debug("failed to initialize WiFi\n");
        return 1;
    }

    run_event_loop(&context.core, coroutine_main(&context.core));

    // unreachable
    cyw43_arch_deinit();
    async_context_deinit(&context.core);
}
