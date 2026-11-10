#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/ledc.h"
#include "toslink_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t pin;              /* GPIO wired to the TOTX input */
    uart_port_t uart_port;       /* any HP UART not used elsewhere */
    ledc_timer_t ledc_timer;     /* LEDC timer reserved for the idle carrier */
    ledc_channel_t ledc_channel; /* LEDC channel reserved for the idle carrier */
} toslink_tx_config_t;

#define TOSLINK_TX_DEFAULT_CONFIG(gpio) {   \
    .pin = (gpio),                          \
    .uart_port = UART_NUM_1,                \
    .ledc_timer = LEDC_TIMER_0,             \
    .ledc_channel = LEDC_CHANNEL_0,         \
}

/* Sets up UART + LEDC and starts the idle carrier on the pin. Call once. */
esp_err_t toslink_tx_init(const toslink_tx_config_t *cfg);

/*
 * Sends one message; blocks ~0.3-0.8 ms. Thread-safe.
 * Returns ESP_ERR_TIMEOUT if the calling task was preempted long enough that
 * the frame may be corrupted on the wire: just send it again.
 */
esp_err_t toslink_tx_send(const tl_msg_t *msg);

#ifdef __cplusplus
}
#endif
