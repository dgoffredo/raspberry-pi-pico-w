#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include <tusb.h>

#include "pico/async_context_poll.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "secrets.h"

#include <array>
#include <cmath>
#include <cstdio>

#define debug(...) printf(__VA_ARGS__)

const char *describe(err_t error) {
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
    return "Unknown error code.";
}

struct Measurement {
    unsigned sequence_number = 0; 
    uint16_t co2_ppm = 0;
    int32_t temperature_millicelsius = 0;
    int32_t relative_humidity_millipercent = 0;
} latest;

template <std::size_t buffer_size>
int format_response(
    std::array<char, buffer_size>& buffer,
    const Measurement& data) {

    // 225 bytes is the largest the response could ever be. I counted.
    static_assert(buffer_size >= 225 + 1);

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
        buffer_size,
        response_format,
        data.sequence_number,
        data.co2_ppm,
        data.temperature_millicelsius / 1000,
        std::abs(data.temperature_millicelsius) % 1000,
        data.relative_humidity_millipercent / 1000,
        std::abs(data.relative_humidity_millipercent) / 1000);
}

struct Client {
    std::array<char, 256> response_buffer;
    int response_length = 0;
    int response_bytes_acked = 0;
};

err_t send_response(Client& client, tcp_pcb *client_pcb) {
    client.response_length = format_response(client.response_buffer, latest);
    debug("Sending response of length %d\n", client.response_length);

    const u8_t flags = 0;
    err_t err = tcp_write(client_pcb, client.response_buffer.data(), client.response_length, flags);
    if (err) {
        debug("tcp_write error: %s\n", describe(err));
        return err;
    }

    err = tcp_output(client_pcb); 	
    if (err) {
        debug("tcp_output error: %s\n", describe(err));
    }

    return err;
}

err_t check(err_t err) {
    if (err) {
        debug("err: %s\n", describe(err));
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
        debug("tcp_close error: %s\n", describe(err));
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
        debug("on_recv error: %s\n", describe(err));
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
        debug("tcp_server_recv %d bytes. err: %d\n", buf->tot_len, err);
        tcp_recved(client_pcb, buf->tot_len);
    }
    pbuf_free(buf);

    return ERR_OK;
}

void on_err(void *arg, err_t err) {
    debug("connection fatal error: %s\n", describe(err));
    delete static_cast<Client*>(arg);
}

err_t on_accept(void *arg, tcp_pcb *client_pcb, err_t err) {
    if (err || client_pcb == NULL) {
        debug("failed to accept a connection: %s\n", describe(err));
        return ERR_VAL; // Is this the appropriate return value?
    }
    debug("client connected\n");

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
        debug("failed to create server pcb\n");
        return;
    }

    err_t err = tcp_bind(pcb, IP_ADDR_ANY, port);
    if (err) {
        debug("failed to bind to port %u: %s\n", port, describe(err));
        return;
    }

    const u8_t backlog = 1;
    pcb = tcp_listen_with_backlog_and_err(pcb, backlog, &err);
    if (!pcb) {
        debug("failed to listen: %s\n", describe(err));
        return;
    }

    tcp_accept(pcb, on_accept);
}

int main() {
    stdio_init_all();

    async_context_poll_t context = {};
    bool succeeded = async_context_poll_init_with_defaults(&context);
    if (!succeeded) {
        debug("Failed to initialize async_context_poll_t\n");
        return -2;
    }

    cyw43_arch_set_async_context(&context.core);

    // 🇺🇸 🦅
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        debug("failed to initialize WiFi\n");
        return 1;
    }

    // Wait for the host to attach to the USB terminal (i.e. ttyACM0).
    while (!tud_cdc_connected()) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(500);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(500);
    }

    cyw43_arch_enable_sta_mode();

    debug("Connecting to WiFi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms("Annoying Saxophone", wifi_password, CYW43_AUTH_WPA2_AES_PSK, 20000)) {
        debug("Failed to connect to WiFi. Trying again in 5 seconds.\n");
        sleep_ms(5 * 1000);
    }

    debug("Connected to WiFi.\n");
    
    setup_http_server(80);

    debug("Entering event loop.\n");
    for (;;) {
      async_context_poll(&context.core);
      async_context_wait_for_work_ms(&context.core, 10 * 1000);
    }

    // unreachable
    cyw43_arch_deinit();
    async_context_deinit(&context.core);
}
