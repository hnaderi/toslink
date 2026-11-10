/*
 * Toslink link demo.
 *   idf.py menuconfig  ->  "Toslink demo"  ->  pick the role and GPIO for each board.
 *
 * TX: every CONFIG_TOSLINK_DEMO_SEND_PERIOD_MS queues command 0x01 carrying a
 *     32-bit counter, CONFIG_TOSLINK_DEMO_REPEATS times (the link is one-way,
 *     so repetition stands in for acknowledgements).
 * RX: prints every new command once, and link statistics every 5 s.
 */
#include <inttypes.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "toslink.h"

static const char *TAG = "demo";

#define CMD_COUNTER 0x01

#if CONFIG_TOSLINK_DEMO_ROLE_TX

void app_main(void)
{
    toslink_tx_config_t cfg = TOSLINK_TX_DEFAULT_CONFIG((gpio_num_t)CONFIG_TOSLINK_DEMO_TX_GPIO);
#if CONFIG_TOSLINK_DEMO_TX_INVERT
    cfg.invert = true;
#endif
    ESP_ERROR_CHECK(toslink_tx_init(&cfg));

    uint8_t seq = 0;
    for (uint32_t counter = 0;; counter++) {
        const tl_msg_t msg = {
            .seq = seq++,
            .cmd = CMD_COUNTER,
            .len = 4,
            .payload = { (uint8_t)(counter >> 24), (uint8_t)(counter >> 16),
                         (uint8_t)(counter >> 8), (uint8_t)counter },
        };

        int queued = 0;
        for (int i = 0; i < CONFIG_TOSLINK_DEMO_REPEATS; i++) {
            if (toslink_tx_send(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
                queued++;
            }
        }
        ESP_LOGI(TAG, "sent seq %u counter %" PRIu32 " (%d copies)", msg.seq, counter, queued);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_TOSLINK_DEMO_SEND_PERIOD_MS));
    }
}

#else /* receiver */

static void on_message(const tl_msg_t *msg, void *user_ctx)
{
    (void)user_ctx;
    if (msg->cmd == CMD_COUNTER && msg->len == 4) {
        const uint32_t v = ((uint32_t)msg->payload[0] << 24) | ((uint32_t)msg->payload[1] << 16) |
                           ((uint32_t)msg->payload[2] << 8) | msg->payload[3];
        ESP_LOGI(TAG, "seq %u counter %" PRIu32, msg->seq, v);
    } else {
        ESP_LOGI(TAG, "seq %u cmd 0x%02x len %u", msg->seq, msg->cmd, msg->len);
    }
}

void app_main(void)
{
    toslink_rx_config_t cfg = TOSLINK_RX_DEFAULT_CONFIG((gpio_num_t)CONFIG_TOSLINK_DEMO_RX_GPIO, on_message);
#if CONFIG_TOSLINK_DEMO_RX_INVERT
    cfg.invert = true;
#endif
    ESP_ERROR_CHECK(toslink_rx_init(&cfg));

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        toslink_rx_stats_t st;
        toslink_rx_get_stats(&st);
        ESP_LOGI(TAG, "stats: ok %" PRIu32 ", duplicates %" PRIu32 ", crc errors %" PRIu32
                 ", symbol errors %" PRIu32 ", uart frame errors %" PRIu32 ", uart overflows %" PRIu32,
                 st.frames_ok, st.duplicates, st.crc_errors, st.symbol_errors,
                 st.uart_frame_errors, st.uart_overflows);
    }
}

#endif
