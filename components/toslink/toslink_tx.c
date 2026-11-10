/*
 * Transmitter: the UART owns the pin permanently and never goes quiet.
 *
 * A feeder task keeps the UART FIFO topped up: with IDLE (0x55) symbols when
 * there is nothing to send, or with [PRE x16][SOF][frame] when a message is
 * queued. The line is therefore always a valid, continuous 8N1 stream; there
 * is no switching between signal sources, so there are no glitches or runt
 * pulses, and the receiver never loses byte alignment.
 *
 * Blocking: with no TX ring buffer, uart_write_bytes() sleeps on a semaphore
 * until the FIFO drains below the empty threshold, then refills it. The task
 * does not busy-wait. With the threshold at 64 bytes the task has ~640 us to
 * react before the FIFO runs dry (at 1 Mbaud).
 */
#include <string.h>
#include "toslink_tx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "toslink_tx";

#define TX_EMPTY_THRESHOLD  64    /* refill when the FIFO drops below this many bytes */
#define TX_IDLE_CHUNK       32    /* idle bytes written per call: bounds message latency */
#define TX_STACK            3072

static struct {
    toslink_tx_config_t cfg;
    QueueHandle_t queue;
} s_tx;

static void tx_task(void *arg)
{
    (void)arg;
    uint8_t idle[TX_IDLE_CHUNK];
    memset(idle, TL_SYM_IDLE, sizeof(idle));
    uint8_t frame[TL_PRE_LEN + TL_MAX_ENCODED];
    memset(frame, TL_SYM_PRE, TL_PRE_LEN);

    for (;;) {
        tl_msg_t msg;
        if (xQueueReceive(s_tx.queue, &msg, 0) == pdTRUE) {
            const size_t n = tl_encode(&msg, &frame[TL_PRE_LEN]);   /* length checked in send() */
            uart_write_bytes(s_tx.cfg.uart_port, frame, TL_PRE_LEN + n);
        } else {
            uart_write_bytes(s_tx.cfg.uart_port, idle, sizeof(idle));
        }
    }
}

esp_err_t toslink_tx_init(const toslink_tx_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg && GPIO_IS_VALID_OUTPUT_GPIO(cfg->pin), ESP_ERR_INVALID_ARG, TAG, "bad pin");
    ESP_RETURN_ON_FALSE(cfg->uart_port >= 0 && cfg->uart_port < SOC_UART_HP_NUM, ESP_ERR_INVALID_ARG, TAG,
                        "bad UART port");
    ESP_RETURN_ON_FALSE(cfg->queue_len > 0, ESP_ERR_INVALID_ARG, TAG, "bad queue length");
    ESP_RETURN_ON_FALSE(s_tx.queue == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialised");

    s_tx.cfg = *cfg;
    s_tx.queue = xQueueCreate((UBaseType_t)cfg->queue_len, sizeof(tl_msg_t));
    ESP_RETURN_ON_FALSE(s_tx.queue, ESP_ERR_NO_MEM, TAG, "queue");

    const uart_config_t uart_cfg = {
        .baud_rate = TL_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    /* TX buffer 0: uart_write_bytes() feeds the HW FIFO directly and sleeps while it is full. */
    ESP_RETURN_ON_ERROR(uart_driver_install(cfg->uart_port, 256, 0, 0, NULL, 0), TAG, "uart install");
    ESP_RETURN_ON_ERROR(uart_param_config(cfg->uart_port, &uart_cfg), TAG, "uart config");
    ESP_RETURN_ON_ERROR(uart_set_pin(cfg->uart_port, cfg->pin, UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart pin");
    if (cfg->invert) {
        ESP_RETURN_ON_ERROR(uart_set_line_inverse(cfg->uart_port, UART_SIGNAL_TXD_INV), TAG, "tx invert");
    }
    ESP_RETURN_ON_ERROR(uart_set_tx_empty_threshold(cfg->uart_port, TX_EMPTY_THRESHOLD), TAG, "tx threshold");

    const BaseType_t core = (cfg->task_core < 0) ? tskNO_AFFINITY : (BaseType_t)cfg->task_core;
    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(tx_task, "toslink_tx", TX_STACK, NULL,
                                                (UBaseType_t)cfg->task_priority, NULL, core) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task");

    ESP_LOGI(TAG, "GPIO%d, %d kbaud, %s", cfg->pin, TL_BAUD / 1000, cfg->invert ? "inverted" : "normal polarity");
    return ESP_OK;
}

esp_err_t toslink_tx_send(const tl_msg_t *msg, TickType_t wait)
{
    ESP_RETURN_ON_FALSE(s_tx.queue, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(msg && msg->len <= TL_MAX_PAYLOAD, ESP_ERR_INVALID_ARG, TAG, "bad message");
    return (xQueueSend(s_tx.queue, msg, wait) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}
