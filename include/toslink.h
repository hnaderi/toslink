#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "toslink_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct toslink_tx *toslink_tx_handle_t;
typedef struct toslink_rx *toslink_rx_handle_t;

typedef struct {
    gpio_num_t pin;        /* GPIO wired to the TOTX input */
    uart_port_t uart_port; /* any HP UART not used elsewhere */
    bool invert;           /* drive the pin active-low, e.g. for an inverting level shifter */
    int task_priority;     /* must refill the UART FIFO within ~100 us, see README */
    int task_core;         /* -1 = no affinity */
    int queue_len;         /* messages that can wait to be sent */
} toslink_tx_config_t;

#define TOSLINK_TX_DEFAULT_CONFIG(gpio)                                                                                \
    {                                                                                                                  \
        .pin = (gpio),                                                                                                 \
        .uart_port = UART_NUM_1,                                                                                       \
        .invert = false,                                                                                               \
        .task_priority = 15,                                                                                           \
        .task_core = -1,                                                                                               \
        .queue_len = 8,                                                                                                \
    }

/* Callbacks run in the receiver task; a slow callback stalls reception (the ring buffer holds ~40 ms). */
typedef void (*toslink_rx_msg_cb_t)(const toslink_msg_t *msg, void *user_ctx);
typedef void (*toslink_rx_link_cb_t)(bool up, void *user_ctx);

typedef struct {
    gpio_num_t pin;        /* GPIO wired to the TORX output */
    uart_port_t uart_port; /* any HP UART not used elsewhere */
    bool invert;           /* signal arrives inverted */
    int task_priority;
    int task_core;       /* -1 = no affinity */
    int task_stack;      /* bytes; the callbacks run on this stack */
    int link_timeout_ms; /* down after this long without a valid stream; resolution is one RTOS tick */
    toslink_rx_msg_cb_t on_message;
    toslink_rx_link_cb_t on_link; /* optional */
    void *user_ctx;
} toslink_rx_config_t;

#define TOSLINK_RX_DEFAULT_CONFIG(gpio, message_cb)                                                                    \
    {                                                                                                                  \
        .pin = (gpio),                                                                                                 \
        .uart_port = UART_NUM_1,                                                                                       \
        .invert = false,                                                                                               \
        .task_priority = 10,                                                                                           \
        .task_core = -1,                                                                                               \
        .task_stack = 3072,                                                                                            \
        .link_timeout_ms = 20,                                                                                         \
        .on_message = (message_cb),                                                                                    \
        .on_link = NULL,                                                                                               \
        .user_ctx = NULL,                                                                                              \
    }

/* Installs the UART driver and starts the task that keeps the line toggling. */
esp_err_t toslink_tx_new(const toslink_tx_config_t *cfg, toslink_tx_handle_t *ret_tx);

/* Stops the task and releases the UART; the pin is left at the UART idle level. No send() may be in progress. */
esp_err_t toslink_tx_del(toslink_tx_handle_t tx);

/*
 * Queues one message; it reaches the wire within ~1.5 ms of reaching the queue head.
 * Returns ESP_ERR_TIMEOUT if the queue stays full for `wait` ticks. Safe from any task.
 */
esp_err_t toslink_tx_send(toslink_tx_handle_t tx, uint8_t cmd, const void *data, size_t len, TickType_t wait);

esp_err_t toslink_rx_new(const toslink_rx_config_t *cfg, toslink_rx_handle_t *ret_rx);

/* Must not be called from a callback. Returns within ~link_timeout_ms / 2. */
esp_err_t toslink_rx_del(toslink_rx_handle_t rx);

esp_err_t toslink_rx_get_stats(toslink_rx_handle_t rx, toslink_rx_stats_t *out);

/* Whether a valid stream is being received. The link starts down. */
bool toslink_rx_link_up(toslink_rx_handle_t rx);

#ifdef __cplusplus
}
#endif
