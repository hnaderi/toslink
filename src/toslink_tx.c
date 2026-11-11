/*
 * The UART owns the pin permanently and never goes quiet: the task keeps the
 * FIFO topped up with IDLE symbols, or with a frame when one is queued. There
 * is no switching between signal sources, so no runt pulses.
 *
 * Gotcha: with no TX ring buffer, uart_write_bytes() re-arms the FIFO-empty
 * interrupt at a fixed 10-byte threshold every time it waits (overriding
 * uart_set_tx_empty_threshold()), so the task must be scheduled within
 * ~100 us of that interrupt or the line pauses.
 */
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "toslink.h"
#include "toslink_proto.h"
#include "toslink_uart.h"

static const char *TAG = "toslink_tx";

#define TX_IDLE_CHUNK 32 /* idle bytes per write: bounds the latency of a queued message */
#define TX_STACK      3072

struct toslink_tx {
    uart_port_t port;
    QueueHandle_t queue;
    SemaphoreHandle_t done;
    volatile bool stop;
};

static void tx_task(void *arg)
{
    toslink_tx_handle_t tx = arg;
    uint8_t idle[TX_IDLE_CHUNK];
    memset(idle, TOSLINK_SYM_IDLE, sizeof(idle));
    uint8_t frame[TOSLINK_MAX_WIRE];
    uint8_t seq = 0;

    while (!tx->stop) {
        toslink_msg_t msg;
        if (xQueueReceive(tx->queue, &msg, 0) == pdTRUE) {
            uart_write_bytes(tx->port, frame, toslink_encode(seq++, &msg, frame));
        } else {
            uart_write_bytes(tx->port, idle, sizeof(idle));
        }
    }
    xSemaphoreGive(tx->done);
    vTaskDelete(NULL);
}

static void tx_free(toslink_tx_handle_t tx)
{
    if (tx->queue) {
        vQueueDelete(tx->queue);
    }
    if (tx->done) {
        vSemaphoreDelete(tx->done);
    }
    free(tx);
}

esp_err_t toslink_tx_new(const toslink_tx_config_t *cfg, toslink_tx_handle_t *ret_tx)
{
    ESP_RETURN_ON_FALSE(cfg && ret_tx && cfg->queue_len > 0, ESP_ERR_INVALID_ARG, TAG, "bad config");

    toslink_tx_handle_t tx = calloc(1, sizeof(*tx));
    ESP_RETURN_ON_FALSE(tx, ESP_ERR_NO_MEM, TAG, "no mem");
    tx->port = cfg->uart_port;
    tx->queue = xQueueCreate((UBaseType_t)cfg->queue_len, sizeof(toslink_msg_t));
    tx->done = xSemaphoreCreateBinary();

    esp_err_t ret;
    ESP_GOTO_ON_FALSE(tx->queue && tx->done, ESP_ERR_NO_MEM, err, TAG, "no mem");
    ESP_GOTO_ON_ERROR(toslink_uart_open(TOSLINK_DIR_TX, cfg->uart_port, cfg->pin, cfg->invert, 256, NULL), err, TAG,
                      "uart");
    ESP_GOTO_ON_ERROR(toslink_task_create(tx_task, "toslink_tx", TX_STACK, tx, cfg->task_priority, cfg->task_core),
                      err_uart, TAG, "task");

    ESP_LOGI(TAG, "UART%d on GPIO%d%s", cfg->uart_port, cfg->pin, cfg->invert ? ", inverted" : "");
    *ret_tx = tx;
    return ESP_OK;

err_uart:
    uart_driver_delete(cfg->uart_port);
err:
    tx_free(tx);
    return ret;
}

esp_err_t toslink_tx_del(toslink_tx_handle_t tx)
{
    ESP_RETURN_ON_FALSE(tx, ESP_ERR_INVALID_ARG, TAG, "null handle");
    tx->stop = true;
    xSemaphoreTake(tx->done, portMAX_DELAY);
    uart_driver_delete(tx->port);
    tx_free(tx);
    return ESP_OK;
}

esp_err_t toslink_tx_send(toslink_tx_handle_t tx, uint8_t cmd, const void *data, size_t len, TickType_t wait)
{
    ESP_RETURN_ON_FALSE(tx && len <= TOSLINK_MAX_PAYLOAD && (data || len == 0), ESP_ERR_INVALID_ARG, TAG,
                        "bad message");
    toslink_msg_t msg = {.cmd = cmd, .len = (uint8_t)len};
    if (len) {
        memcpy(msg.payload, data, len);
    }
    return (xQueueSend(tx->queue, &msg, wait) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}
