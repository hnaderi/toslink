/*
 * Link test. The transmitter sends a few text lines, then saturates the link
 * for a burst (or forever in soak mode). The receiver prints the lines,
 * verifies every burst chunk, logs link changes and prints running totals
 * once per second while data arrives.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "toslink.h"

#if TOSLINK_MAX_PAYLOAD < 2
#error "link_test needs CONFIG_TOSLINK_MAX_PAYLOAD >= 2"
#endif

#define CMD_TEXT_LINE 0x10 /* printed by the receiver */
#define CMD_TEXT_BULK 0x11 /* payload[0] = chunk counter, rest derived from it */

#define BULK_TEXT_LEN (TOSLINK_MAX_PAYLOAD - 1)

#ifdef CONFIG_TOSLINK_TEST_INVERT
#define TEST_INVERT true
#else
#define TEST_INVERT false
#endif

static const char *TAG = "link_test";

static void bulk_text(uint8_t counter, uint8_t *out)
{
    static const char sentence[] = "The quick brown fox jumps over the lazy dog. ";
    for (int i = 0; i < BULK_TEXT_LEN; i++) {
        out[i] = (uint8_t)sentence[(counter * BULK_TEXT_LEN + i) % (sizeof(sentence) - 1)];
    }
}

#if CONFIG_TOSLINK_TEST_ROLE_TX

#if !CONFIG_TOSLINK_TEST_SOAK
static void send_text(toslink_tx_handle_t tx, const char *text, size_t len)
{
    for (size_t off = 0; off < len; off += TOSLINK_MAX_PAYLOAD) {
        const size_t n = (len - off) < TOSLINK_MAX_PAYLOAD ? (len - off) : TOSLINK_MAX_PAYLOAD;
        ESP_ERROR_CHECK(toslink_tx_send(tx, CMD_TEXT_LINE, text + off, n, portMAX_DELAY));
    }
}
#endif

/* Saturates the link for duration_us (forever if negative). */
static void send_bulk(toslink_tx_handle_t tx, int64_t duration_us)
{
    uint32_t chunks = 0, reported = 0;
    const int64_t t0 = esp_timer_get_time();
    int64_t t_report = t0;
    for (;;) {
        const int64_t now = esp_timer_get_time();
        if (duration_us >= 0 && now - t0 >= duration_us) {
            break;
        }
        if (now - t_report >= 5000000) {
            ESP_LOGI(TAG, "sent %" PRIu32 " chunks, %.1f kB/s payload", chunks,
                     (chunks - reported) * TOSLINK_MAX_PAYLOAD / ((now - t_report) / 1e6) / 1000.0);
            reported = chunks;
            t_report = now;
        }
        uint8_t chunk[TOSLINK_MAX_PAYLOAD] = {(uint8_t)chunks};
        bulk_text(chunk[0], &chunk[1]);
        ESP_ERROR_CHECK(toslink_tx_send(tx, CMD_TEXT_BULK, chunk, sizeof(chunk), portMAX_DELAY));
        chunks++;
    }
}

void app_main(void)
{
    toslink_tx_config_t cfg = TOSLINK_TX_DEFAULT_CONFIG(CONFIG_TOSLINK_TEST_GPIO);
    cfg.invert = TEST_INVERT;
    toslink_tx_handle_t tx;
    ESP_ERROR_CHECK(toslink_tx_new(&cfg, &tx));
    ESP_LOGI(TAG, "transmitter, %d-byte payload", TOSLINK_MAX_PAYLOAD);

#if CONFIG_TOSLINK_TEST_SOAK
    send_bulk(tx, -1);
#else
    for (uint32_t cycle = 1;; cycle++) {
        char line[96];
        for (int i = 1; i <= 3; i++) {
            const int n = snprintf(line, sizeof(line), "Cycle %" PRIu32 ", line %d: hello over Toslink!\n", cycle, i);
            send_text(tx, line, (size_t)n);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        send_bulk(tx, (int64_t)CONFIG_TOSLINK_TEST_BURST_MS * 1000);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
#endif
}

#else

/* Written by the RX task, read by app_main. */
static volatile uint32_t s_bulk_bytes, s_bulk_bad;
static char s_line[128];
static size_t s_line_len;

static void on_message(const toslink_msg_t *msg, void *ctx)
{
    (void)ctx;
    if (msg->cmd == CMD_TEXT_BULK) {
        uint8_t expect[BULK_TEXT_LEN];
        bulk_text(msg->payload[0], expect);
        if (msg->len != TOSLINK_MAX_PAYLOAD || memcmp(&msg->payload[1], expect, sizeof(expect)) != 0) {
            s_bulk_bad++;
        }
        s_bulk_bytes += msg->len;
    } else if (msg->cmd == CMD_TEXT_LINE) {
        for (int i = 0; i < msg->len; i++) {
            const char c = (char)msg->payload[i];
            if (c == '\n' || s_line_len == sizeof(s_line) - 1) {
                s_line[s_line_len] = '\0';
                ESP_LOGI(TAG, "text: %s", s_line);
                s_line_len = 0;
            } else {
                s_line[s_line_len++] = c;
            }
        }
    }
}

static void on_link(bool up, void *ctx)
{
    (void)ctx;
    ESP_LOGW(TAG, "link %s", up ? "up" : "down");
}

void app_main(void)
{
    toslink_rx_config_t cfg = TOSLINK_RX_DEFAULT_CONFIG(CONFIG_TOSLINK_TEST_GPIO, on_message);
    cfg.invert = TEST_INVERT;
    cfg.on_link = on_link;
    toslink_rx_handle_t rx;
    ESP_ERROR_CHECK(toslink_rx_new(&cfg, &rx));
    ESP_LOGI(TAG, "receiver, %d-byte payload", TOSLINK_MAX_PAYLOAD);

    uint32_t last_bytes = 0;
    int64_t last_t = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        const int64_t now = esp_timer_get_time();
        const uint32_t bytes = s_bulk_bytes;
        const double rate = (bytes - last_bytes) / ((double)(now - last_t) / 1e6) / 1000.0;
        last_bytes = bytes;
        last_t = now;
        if (rate == 0.0) {
            continue;
        }

        toslink_rx_stats_t st;
        ESP_ERROR_CHECK(toslink_rx_get_stats(rx, &st));
        ESP_LOGI(TAG,
                 "%.1f kB/s | ok %" PRIu32 " bad %" PRIu32 " lost %" PRIu32 " | crc %" PRIu32 " sym %" PRIu32
                 " uart-fe %" PRIu32 " ovf %" PRIu32 " link-losses %" PRIu32,
                 rate, st.frames_ok, s_bulk_bad, st.frames_lost, st.crc_errors, st.symbol_errors, st.uart_frame_errors,
                 st.uart_overflows, st.link_losses);
    }
}

#endif
