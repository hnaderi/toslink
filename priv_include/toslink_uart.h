#pragma once

#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef enum { TOSLINK_DIR_TX, TOSLINK_DIR_RX } toslink_dir_t;

/* Installs the driver and configures 8N1 at TOSLINK_BAUD on one pin. Deletes the driver again on failure. */
esp_err_t toslink_uart_open(toslink_dir_t dir, uart_port_t port, gpio_num_t pin, bool invert, int rx_buf_len,
                            QueueHandle_t *events);

esp_err_t toslink_task_create(TaskFunction_t fn, const char *name, int stack, void *arg, int priority, int core);
