/*
 * The hardware UART does the bit-level work; the task only feeds bytes to the
 * parser and the link monitor.
 *
 * The RX FIFO full threshold is lowered from the driver's 120 (of 128) to 64:
 * at 1 Mbaud the default leaves only 80 us to service the interrupt before an
 * overflow, and an overflow makes the driver reset the FIFO.
 */
#include <stdlib.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "toslink.h"
#include "toslink_proto.h"
#include "toslink_uart.h"

static const char *TAG = "toslink_rx";

#define RX_RINGBUF_LEN    4096 /* ~40 ms of line data at 1 Mbaud */
#define RX_CHUNK          64   /* read size; bounds the added latency to ~0.64 ms */
#define RX_FULL_THRESHOLD 64

struct toslink_rx {
    uart_port_t port;
    QueueHandle_t events;
    SemaphoreHandle_t done;
    volatile bool stop;
    TickType_t read_timeout;
    int64_t link_timeout_us;
    toslink_rx_msg_cb_t on_message;
    toslink_rx_link_cb_t on_link;
    void *user_ctx;
    toslink_parser_t parser; /* written only by the RX task */
    toslink_link_t link;
};

static void drain_events(toslink_rx_handle_t rx)
{
    uart_event_t ev;
    while (xQueueReceive(rx->events, &ev, 0) == pdTRUE) {
        switch (ev.type) {
        case UART_FRAME_ERR:
        case UART_BREAK:
            rx->parser.stats.uart_frame_errors++;
            toslink_link_symbol(&rx->link, false);
            break;
        case UART_BUFFER_FULL:
            uart_flush_input(rx->port); /* the driver stops receiving until space is made */
            /* fall through */
        case UART_FIFO_OVF:
            rx->parser.stats.uart_overflows++;
            toslink_parser_reset(&rx->parser);
            break;
        default:
            break;
        }
    }
}

static void poll_link(toslink_rx_handle_t rx)
{
    const toslink_link_event_t ev = toslink_link_poll(&rx->link, esp_timer_get_time(), rx->link_timeout_us);
    if (ev == TOSLINK_LINK_NONE) {
        return;
    }
    if (ev == TOSLINK_LINK_DOWN) {
        rx->parser.stats.link_losses++;
        toslink_parser_reset(&rx->parser);
    }
    if (rx->on_link) {
        rx->on_link(ev == TOSLINK_LINK_UP, rx->user_ctx);
    }
}

static void rx_task(void *arg)
{
    toslink_rx_handle_t rx = arg;
    uint8_t buf[RX_CHUNK];

    while (!rx->stop) {
        /* A dead line returns nothing; the timeout keeps the link monitor and del() responsive. */
        const int n = uart_read_bytes(rx->port, buf, sizeof(buf), rx->read_timeout);
        for (int i = 0; i < n; i++) {
            toslink_link_symbol(&rx->link, toslink_symbol_valid(buf[i]));
            toslink_msg_t msg;
            if (toslink_parser_feed(&rx->parser, buf[i], &msg)) {
                rx->on_message(&msg, rx->user_ctx);
            }
        }
        drain_events(rx);
        poll_link(rx);
    }
    xSemaphoreGive(rx->done);
    vTaskDelete(NULL);
}

esp_err_t toslink_rx_new(const toslink_rx_config_t *cfg, toslink_rx_handle_t *ret_rx)
{
    ESP_RETURN_ON_FALSE(cfg && ret_rx && cfg->on_message && cfg->task_stack > 0 && cfg->link_timeout_ms > 0,
                        ESP_ERR_INVALID_ARG, TAG, "bad config");

    toslink_rx_handle_t rx = calloc(1, sizeof(*rx));
    ESP_RETURN_ON_FALSE(rx, ESP_ERR_NO_MEM, TAG, "no mem");
    rx->port = cfg->uart_port;
    rx->on_message = cfg->on_message;
    rx->on_link = cfg->on_link;
    rx->user_ctx = cfg->user_ctx;
    rx->link_timeout_us = (int64_t)cfg->link_timeout_ms * 1000;
    rx->read_timeout = pdMS_TO_TICKS(cfg->link_timeout_ms / 2);
    if (rx->read_timeout == 0) {
        rx->read_timeout = 1;
    }
    rx->done = xSemaphoreCreateBinary();

    esp_err_t ret;
    ESP_GOTO_ON_FALSE(rx->done, ESP_ERR_NO_MEM, err, TAG, "no mem");
    ESP_GOTO_ON_ERROR(
        toslink_uart_open(TOSLINK_DIR_RX, cfg->uart_port, cfg->pin, cfg->invert, RX_RINGBUF_LEN, &rx->events), err, TAG,
        "uart");
    ESP_GOTO_ON_ERROR(uart_set_rx_full_threshold(cfg->uart_port, RX_FULL_THRESHOLD), err_uart, TAG, "rx threshold");
    ESP_GOTO_ON_ERROR(
        toslink_task_create(rx_task, "toslink_rx", cfg->task_stack, rx, cfg->task_priority, cfg->task_core), err_uart,
        TAG, "task");

    ESP_LOGI(TAG, "UART%d on GPIO%d%s", cfg->uart_port, cfg->pin, cfg->invert ? ", inverted" : "");
    *ret_rx = rx;
    return ESP_OK;

err_uart:
    uart_driver_delete(cfg->uart_port); /* also deletes the event queue */
err:
    if (rx->done) {
        vSemaphoreDelete(rx->done);
    }
    free(rx);
    return ret;
}

esp_err_t toslink_rx_del(toslink_rx_handle_t rx)
{
    ESP_RETURN_ON_FALSE(rx, ESP_ERR_INVALID_ARG, TAG, "null handle");
    rx->stop = true;
    xSemaphoreTake(rx->done, portMAX_DELAY);
    uart_driver_delete(rx->port);
    vSemaphoreDelete(rx->done);
    free(rx);
    return ESP_OK;
}

esp_err_t toslink_rx_get_stats(toslink_rx_handle_t rx, toslink_rx_stats_t *out)
{
    ESP_RETURN_ON_FALSE(rx && out, ESP_ERR_INVALID_ARG, TAG, "bad arg");
    /* Fields are 32-bit and written only by the RX task: each is consistent, the set may be a few bytes apart. */
    *out = rx->parser.stats;
    return ESP_OK;
}

bool toslink_rx_link_up(toslink_rx_handle_t rx)
{
    return rx && rx->link.up;
}
