/*
 * Transmitter: LEDC and UART share one pin through the GPIO matrix.
 *
 *   idle  : pin <- LEDC, 50 % square wave at TL_BAUD/2 (looks like a stream of 0x55)
 *   frame : pin <- UART for [PRE.. SOF DATA.. IDLE..], then back to LEDC
 *
 * The whole transmission is written into the UART hardware FIFO in one go,
 * so the UART timing is exact. Both hand-overs happen while the UART is
 * sending filler (PRE at the start, IDLE at the end), never inside the frame.
 * A switch only shifts the waveform phase, giving at most one pulse of about
 * 6 bit times (6 us); the receiver re-aligns on the PRE bytes.
 *
 * The GPIO-matrix signal numbers are not hard-coded: the UART and LEDC drivers
 * each route the pin themselves during init, and the signal they connected is
 * read back with gpio_get_io_config(). Only public, stable APIs are used.
 */
#include <inttypes.h>
#include <string.h>
#include "toslink_tx.h"

#include "freertos/FreeRTOS.h"   /* v6 driver headers no longer include these implicitly */
#include "freertos/semphr.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "soc/gpio_sig_map.h"   /* SIG_GPIO_OUT_IDX */

static const char *TAG = "toslink_tx";

#define TX_LEDC_MODE   LEDC_LOW_SPEED_MODE
#define TX_BUF_LEN     (TL_PRE_LEN + TL_MAX_ENCODED + TL_POST_LEN)

static struct {
    toslink_tx_config_t cfg;
    uint32_t sig_uart;
    uint32_t sig_ledc;
    SemaphoreHandle_t lock;
} s_tx;

static inline void route_pin(uint32_t signal)
{
    esp_rom_gpio_connect_out_signal((uint32_t)s_tx.cfg.pin, signal, false, false);
}

esp_err_t toslink_tx_init(const toslink_tx_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg && GPIO_IS_VALID_OUTPUT_GPIO(cfg->pin), ESP_ERR_INVALID_ARG, TAG, "bad pin");
    ESP_RETURN_ON_FALSE(cfg->uart_port >= 0 && cfg->uart_port < SOC_UART_HP_NUM, ESP_ERR_INVALID_ARG, TAG,
                        "bad UART port");
    ESP_RETURN_ON_FALSE(s_tx.lock == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    ESP_RETURN_ON_FALSE(TX_BUF_LEN <= UART_HW_FIFO_LEN(cfg->uart_port), ESP_ERR_INVALID_SIZE, TAG,
                        "a whole transmission must fit in the UART hardware FIFO");

    s_tx.cfg = *cfg;
    s_tx.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_tx.lock, ESP_ERR_NO_MEM, TAG, "mutex");

    /* UART with no TX ring buffer: uart_write_bytes() goes straight into the HW FIFO.
       With no RX pin assigned, the driver itself ties the RX input high. */
    const uart_config_t uart_cfg = {
        .baud_rate = TL_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(cfg->uart_port, 256, 0, 0, NULL, 0), TAG, "uart install");
    ESP_RETURN_ON_ERROR(uart_param_config(cfg->uart_port, &uart_cfg), TAG, "uart config");

    /* 1) Let the UART driver route TX to the pin and note which signal it used. */
    gpio_io_config_t io;
    ESP_RETURN_ON_ERROR(uart_set_pin(cfg->uart_port, cfg->pin, UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart pin");
    ESP_RETURN_ON_ERROR(gpio_get_io_config(cfg->pin, &io), TAG, "read io config");
    s_tx.sig_uart = io.sig_out;

    /* 2) LEDC: 1-bit resolution, duty 1 of 2 = 50 %, at half the baud rate.
          Configuring the channel re-routes the pin to LEDC (idle carrier starts). */
    const ledc_timer_config_t timer_cfg = {
        .speed_mode = TX_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_1_BIT,
        .timer_num = cfg->ledc_timer,
        .freq_hz = TL_BAUD / 2,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "ledc timer");
    const ledc_channel_config_t ch_cfg = {
        .gpio_num = cfg->pin,
        .speed_mode = TX_LEDC_MODE,
        .channel = cfg->ledc_channel,
        .timer_sel = cfg->ledc_timer,
        .duty = 1,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch_cfg), TAG, "ledc channel");
    ESP_RETURN_ON_ERROR(gpio_get_io_config(cfg->pin, &io), TAG, "read io config");
    s_tx.sig_ledc = io.sig_out;

    /* If the UART reached the pin through its dedicated IO-MUX path instead of the
       GPIO matrix, there is no matrix signal to switch back to. */
    ESP_RETURN_ON_FALSE(s_tx.sig_uart != SIG_GPIO_OUT_IDX && s_tx.sig_ledc != SIG_GPIO_OUT_IDX &&
                        s_tx.sig_uart != s_tx.sig_ledc, ESP_ERR_NOT_SUPPORTED, TAG,
                        "GPIO%d cannot be switched between UART and LEDC (IO-MUX pin?); use another GPIO",
                        cfg->pin);

    ESP_LOGI(TAG, "idle carrier on GPIO%d, %d kbaud", cfg->pin, TL_BAUD / 1000);
    return ESP_OK;
}

esp_err_t toslink_tx_send(const tl_msg_t *msg)
{
    ESP_RETURN_ON_FALSE(s_tx.lock, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(msg, ESP_ERR_INVALID_ARG, TAG, "msg");

    uint8_t buf[TX_BUF_LEN];
    memset(buf, TL_SYM_PRE, TL_PRE_LEN);
    const size_t frame_len = tl_encode(msg, &buf[TL_PRE_LEN]);
    ESP_RETURN_ON_FALSE(frame_len > 0, ESP_ERR_INVALID_ARG, TAG, "payload too long");
    const size_t total = TL_PRE_LEN + frame_len + TL_POST_LEN;
    memset(&buf[TL_PRE_LEN + frame_len], TL_SYM_IDLE, TL_POST_LEN);

    /* Deadlines relative to the moment the UART starts shifting. */
    const int64_t latest_in  = (TL_PRE_LEN / 2) * TL_BYTE_US;                  /* >= 8 PRE still on the wire */
    const int64_t switch_out = (int64_t)(TL_PRE_LEN + frame_len + 1) * TL_BYTE_US; /* 1 IDLE byte of margin */
    const int64_t latest_out = (int64_t)(total - 2) * TL_BYTE_US;              /* before the UART goes quiet */
    const uart_port_t port = s_tx.cfg.uart_port;

    xSemaphoreTake(s_tx.lock, portMAX_DELAY);
    uart_wait_tx_done(port, portMAX_DELAY);           /* previous postamble fully drained */

    const int64_t t0 = esp_timer_get_time();
    uart_write_bytes(port, buf, total);               /* fits in the FIFO: returns right away */
    route_pin(s_tx.sig_uart);                         /* pin follows the UART (still in PRE) */
    const int64_t t_in = esp_timer_get_time() - t0;

    const int64_t remaining = switch_out - (esp_timer_get_time() - t0);
    if (remaining > 0) {
        esp_rom_delay_us((uint32_t)remaining);        /* sub-millisecond: busy-wait is fine */
    }
    route_pin(s_tx.sig_ledc);                         /* back to the idle carrier */
    const int64_t t_out = esp_timer_get_time() - t0;
    xSemaphoreGive(s_tx.lock);

    if (t_in > latest_in || t_out > latest_out) {
        ESP_LOGW(TAG, "preempted (in %" PRId64 " us, out %" PRId64 " us), frame may be lost", t_in, t_out);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
