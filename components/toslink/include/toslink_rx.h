#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "toslink_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called from the receiver task for each new message; repeats (same seq) are filtered out. */
typedef void (*toslink_rx_cb_t)(const tl_msg_t *msg, void *user_ctx);

typedef struct {
    gpio_num_t pin;          /* GPIO wired to the TORX output */
    uart_port_t uart_port;   /* any HP UART not used elsewhere */
    int task_priority;
    int task_core;           /* -1 = no affinity */
    uint32_t dup_window_ms;  /* same seq within this window counts as a repeat */
    toslink_rx_cb_t cb;
    void *user_ctx;
} toslink_rx_config_t;

#define TOSLINK_RX_DEFAULT_CONFIG(gpio, callback) { \
    .pin = (gpio),                                  \
    .uart_port = UART_NUM_1,                        \
    .task_priority = 10,                            \
    .task_core = -1,                                \
    .dup_window_ms = 200,                           \
    .cb = (callback),                               \
    .user_ctx = NULL,                               \
}

typedef struct {
    uint32_t frames_ok;
    uint32_t duplicates;
    uint32_t crc_errors;
    uint32_t symbol_errors;  /* invalid symbol or length inside a frame */
} toslink_rx_stats_t;

/* Sets up the UART and starts the receiver task. Call once. */
esp_err_t toslink_rx_init(const toslink_rx_config_t *cfg);

void toslink_rx_get_stats(toslink_rx_stats_t *out);

#ifdef __cplusplus
}
#endif
