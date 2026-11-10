#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "toslink_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t pin;          /* GPIO wired to the TOTX input */
    uart_port_t uart_port;   /* any HP UART not used elsewhere */
    bool invert;             /* true: pin is active-low (for an inverting level shifter) */
    int task_priority;       /* feeder task; must refill the FIFO within ~0.6 ms */
    int task_core;           /* -1 = no affinity */
    int queue_len;           /* messages that can wait to be sent */
} toslink_tx_config_t;

#define TOSLINK_TX_DEFAULT_CONFIG(gpio) {   \
    .pin = (gpio),                          \
    .uart_port = UART_NUM_1,                \
    .invert = false,                        \
    .task_priority = 15,                    \
    .task_core = -1,                        \
    .queue_len = 8,                         \
}

/* Sets up the UART and starts the feeder task that keeps the line toggling. Call once. */
esp_err_t toslink_tx_init(const toslink_tx_config_t *cfg);

/*
 * Queues one message for transmission. It goes on the wire within ~1.5 ms.
 * Blocks while the queue is full for up to `wait` ticks; returns
 * ESP_ERR_TIMEOUT if it is still full. Safe to call from any task.
 */
esp_err_t toslink_tx_send(const tl_msg_t *msg, TickType_t wait);

#ifdef __cplusplus
}
#endif
