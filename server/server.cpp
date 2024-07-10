/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <tusb.h>

#include "pico/async_context_poll.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "secrets.h"

#define DEBUG_printf(...) printf(__VA_ARGS__)

constexpr uint16_t TCP_PORT = 80;
constexpr long  BUF_SIZE = 2048;

typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    struct tcp_pcb *client_pcb;
    bool complete;
    uint8_t buffer_sent[BUF_SIZE];
    uint8_t buffer_recv[BUF_SIZE];
    int sent_len;
    int recv_len;
    int run_count;
} TCP_SERVER_T;

static TCP_SERVER_T* tcp_server_init() {
    TCP_SERVER_T *state = (TCP_SERVER_T*)calloc(1, sizeof(TCP_SERVER_T));
    if (!state) {
        DEBUG_printf("failed to allocate state\n");
        return NULL;
    }
    return state;
}

err_t tcp_server_close(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    err_t err = ERR_OK;
    if (state->client_pcb != NULL) {
        tcp_arg(state->client_pcb, NULL);
        tcp_poll(state->client_pcb, NULL, 0);
        tcp_sent(state->client_pcb, NULL);
        tcp_recv(state->client_pcb, NULL);
        tcp_err(state->client_pcb, NULL);
        err = tcp_close(state->client_pcb);
        if (err != ERR_OK) {
            DEBUG_printf("close failed %d, calling abort\n", err);
            tcp_abort(state->client_pcb);
            err = ERR_ABRT;
        }
        state->client_pcb = NULL;
    }
    if (state->server_pcb) {
        tcp_arg(state->server_pcb, NULL);
        tcp_close(state->server_pcb);
        state->server_pcb = NULL;
    }
    return err;
}

err_t tcp_server_done(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    DEBUG_printf("Server go bye-bye.\n");
    state->complete = true;
    return tcp_server_close(arg);
}

err_t tcp_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    DEBUG_printf("tcp_server_sent %u\n", len);
    state->sent_len += len;

    // TODO: detect whether we sent the whole payload
    // if (state->sent_len >= BUF_SIZE) {
    // }

    return ERR_OK;
}

err_t tcp_server_send_data(void *arg, struct tcp_pcb *tpcb)
{
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    static const char message[] = "Hello, and goodbye!\n";
    constexpr long MSG_LEN = sizeof(message) - 1;
    for(int i = 0; i < MSG_LEN; ++i) {
        state->buffer_sent[i] = message[i];
    }

    state->sent_len = 0;
    DEBUG_printf("Writing %ld bytes to client\n", MSG_LEN);

    err_t err = tcp_write(tpcb, state->buffer_sent, MSG_LEN, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        DEBUG_printf("Failed to write data %d\n", err);
        return tcp_server_done(arg);
    }
    return ERR_OK;
}

err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    if (!p) {
        return tcp_server_done(arg);
    }

    if (p->tot_len > 0) {
        DEBUG_printf("tcp_server_recv %d/%d err %d\n", p->tot_len, state->recv_len, err);
        // Consume the buffer.
        tcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);

    return ERR_OK;
}

void tcp_server_err(void *arg, err_t err) {
    if (err != ERR_ABRT) {
        DEBUG_printf("tcp_client_err_fn %d\n", err);
        tcp_server_done(arg);
    }
}

err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    if (err != ERR_OK || client_pcb == NULL) {
        DEBUG_printf("Failure in accept\n");
        tcp_server_done(arg);
        return ERR_VAL;
    }
    DEBUG_printf("Client connected\n");

    state->client_pcb = client_pcb;
    tcp_arg(client_pcb, state);
    tcp_sent(client_pcb, tcp_server_sent);
    tcp_recv(client_pcb, tcp_server_recv);
    tcp_err(client_pcb, tcp_server_err);

    return tcp_server_send_data(arg, state->client_pcb);
}

bool tcp_server_open(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    DEBUG_printf("Starting server at %s on port %u\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), TCP_PORT);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        DEBUG_printf("failed to create pcb\n");
        return false;
    }

    err_t err = tcp_bind(pcb, NULL, TCP_PORT);
    if (err) {
        DEBUG_printf("failed to bind to port %u\n", TCP_PORT);
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        DEBUG_printf("failed to listen\n");
        if (pcb) {
            tcp_close(pcb);
        }
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, tcp_server_accept);

    return true;
}

void run_tcp_server_test(async_context_t *context) {
    TCP_SERVER_T *state = tcp_server_init();
    if (!state) {
        return;
    }
    if (!tcp_server_open(state)) {
        tcp_server_done(state);
        return;
    }
    while (!state->complete) {
        async_context_poll(context);
        async_context_wait_for_work_ms(context, 10 * 1000);
    }
    free(state);
}

int main() {
    stdio_init_all();

    async_context_poll_t context = {};
    bool succeeded = async_context_poll_init_with_defaults(&context);
    if (!succeeded) {
        printf("Failed to initialize async_context_poll_t\n");
        return -2;
    }

    cyw43_arch_set_async_context(&context.core);

    // 🇺🇸 🦅
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        printf("failed to initialize WiFi\n");
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

    printf("Connecting to WiFi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms("Annoying Saxophone", wifi_password, CYW43_AUTH_WPA2_AES_PSK, 20000)) {
        printf("Failed to connect to WiFi. Trying again in 5 seconds.\n");
        sleep_ms(5 * 1000);
    }

    printf("Connected to WiFi.\n");
    run_tcp_server_test(&context.core);

    cyw43_arch_deinit();
    async_context_deinit(&context.core);
}
