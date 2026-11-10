/*
 * Receiver: the hardware UART does the bit-level work; this task only scans
 * the byte stream. Between messages the stream is IDLE (0x55) symbols, which
 * hit the first test in parse() and are dropped, so the idle load is a few
 * hundred task wake-ups per second.
 */
#include <string.h>
#include "toslink_rx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "toslink_rx";

#define RX_RINGBUF_LEN   4096   /* ~40 ms of line data at 1 Mbaud */
#define RX_CHUNK         256
#define RX_STACK         3072

typedef enum { ST_HUNT, ST_BODY } rx_state_t;

static struct {
    toslink_rx_config_t cfg;
    bool running;
    rx_state_t state;
    uint8_t pre_seen;
    bool have_high_nibble;
    uint8_t cur;
    uint8_t raw[TL_MAX_RAW];
    size_t got;
    bool have_last;
    uint8_t last_seq;
    int64_t last_time;
    toslink_rx_stats_t stats;
} s_rx;

static void deliver(void)
{
    const size_t body = TL_HDR_LEN + s_rx.raw[0];
    const uint16_t crc = (uint16_t)((s_rx.raw[body] << 8) | s_rx.raw[body + 1]);
    if (tl_crc16(s_rx.raw, body) != crc) {
        s_rx.stats.crc_errors++;
        return;
    }

    tl_msg_t msg = { .len = s_rx.raw[0], .seq = s_rx.raw[1], .cmd = s_rx.raw[2] };
    memcpy(msg.payload, &s_rx.raw[TL_HDR_LEN], msg.len);

    const int64_t now = esp_timer_get_time();
    const int64_t window = (int64_t)s_rx.cfg.dup_window_ms * 1000;
    if (s_rx.have_last && msg.seq == s_rx.last_seq && now - s_rx.last_time < window) {
        s_rx.stats.duplicates++;
        s_rx.last_time = now;
        return;
    }
    s_rx.have_last = true;
    s_rx.last_seq = msg.seq;
    s_rx.last_time = now;
    s_rx.stats.frames_ok++;
    if (s_rx.cfg.cb) {
        s_rx.cfg.cb(&msg, s_rx.cfg.user_ctx);
    }
}

static void parse(uint8_t b)
{
    if (s_rx.state == ST_HUNT) {
        if (b == TL_SYM_IDLE) {                    /* the common case: nothing to do */
            s_rx.pre_seen = 0;
        } else if (b == TL_SYM_PRE) {
            if (s_rx.pre_seen < UINT8_MAX) {
                s_rx.pre_seen++;
            }
        } else {
            if (b == TL_SYM_SOF && s_rx.pre_seen >= TL_PRE_MIN_SEEN) {
                s_rx.state = ST_BODY;
                s_rx.got = 0;
                s_rx.have_high_nibble = false;
            }
            s_rx.pre_seen = 0;
        }
        return;
    }

    const int nib = tl_decode_symbol(b);
    if (nib < 0) {                                 /* broken frame: back to hunting */
        s_rx.stats.symbol_errors++;
        s_rx.state = ST_HUNT;
        s_rx.pre_seen = (b == TL_SYM_PRE) ? 1 : 0;
        return;
    }
    if (!s_rx.have_high_nibble) {
        s_rx.cur = (uint8_t)(nib << 4);
        s_rx.have_high_nibble = true;
        return;
    }
    s_rx.have_high_nibble = false;
    s_rx.raw[s_rx.got++] = (uint8_t)(s_rx.cur | nib);

    if (s_rx.got == 1 && s_rx.raw[0] > TL_MAX_PAYLOAD) {
        s_rx.stats.symbol_errors++;
        s_rx.state = ST_HUNT;
    } else if (s_rx.got == (size_t)(TL_HDR_LEN + s_rx.raw[0] + TL_CRC_LEN)) {
        deliver();
        s_rx.state = ST_HUNT;
    }
}

static void rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[RX_CHUNK];
    for (;;) {
        /* Returns when the chunk is full (~every 2.5 ms at 1 Mbaud) or on timeout. */
        const int n = uart_read_bytes(s_rx.cfg.uart_port, buf, sizeof(buf), pdMS_TO_TICKS(20));
        for (int i = 0; i < n; i++) {
            parse(buf[i]);
        }
    }
}

esp_err_t toslink_rx_init(const toslink_rx_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg && GPIO_IS_VALID_GPIO(cfg->pin), ESP_ERR_INVALID_ARG, TAG, "bad pin");
    ESP_RETURN_ON_FALSE(cfg->uart_port >= 0 && cfg->uart_port < SOC_UART_HP_NUM, ESP_ERR_INVALID_ARG, TAG,
                        "bad UART port");
    ESP_RETURN_ON_FALSE(!s_rx.running, ESP_ERR_INVALID_STATE, TAG, "already initialised");

    memset(&s_rx, 0, sizeof(s_rx));
    s_rx.cfg = *cfg;

    const uart_config_t uart_cfg = {
        .baud_rate = TL_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(cfg->uart_port, RX_RINGBUF_LEN, 0, 0, NULL, 0), TAG, "uart install");
    ESP_RETURN_ON_ERROR(uart_param_config(cfg->uart_port, &uart_cfg), TAG, "uart config");
    ESP_RETURN_ON_ERROR(uart_set_pin(cfg->uart_port, UART_PIN_NO_CHANGE, cfg->pin,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart pin");

    const BaseType_t core = (cfg->task_core < 0) ? tskNO_AFFINITY : (BaseType_t)cfg->task_core;
    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(rx_task, "toslink_rx", RX_STACK, NULL,
                                                (UBaseType_t)cfg->task_priority, NULL, core) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task");
    s_rx.running = true;
    ESP_LOGI(TAG, "listening on GPIO%d, %d kbaud", cfg->pin, TL_BAUD / 1000);
    return ESP_OK;
}

void toslink_rx_get_stats(toslink_rx_stats_t *out)
{
    /* Counters are written only by the RX task; a torn read just shows a slightly old value. */
    *out = s_rx.stats;
}
