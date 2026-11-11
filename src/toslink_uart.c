#include "toslink_uart.h"
#include "esp_check.h"
#include "soc/soc_caps.h"
#include "toslink_proto.h"

static const char *TAG = "toslink";

esp_err_t toslink_uart_open(toslink_dir_t dir, uart_port_t port, gpio_num_t pin, bool invert, int rx_buf_len,
                            QueueHandle_t *events)
{
    const bool tx = (dir == TOSLINK_DIR_TX);
    ESP_RETURN_ON_FALSE(tx ? GPIO_IS_VALID_OUTPUT_GPIO(pin) : GPIO_IS_VALID_GPIO(pin), ESP_ERR_INVALID_ARG, TAG,
                        "bad pin");
    ESP_RETURN_ON_FALSE(port >= 0 && port < SOC_UART_HP_NUM, ESP_ERR_INVALID_ARG, TAG, "bad UART port");

    const uart_config_t cfg = {
        .baud_rate = TOSLINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    /* No TX ring buffer: uart_write_bytes() feeds the FIFO directly and sleeps while it is full. */
    ESP_RETURN_ON_ERROR(uart_driver_install(port, rx_buf_len, 0, events ? 64 : 0, events, 0), TAG, "uart install");

    esp_err_t ret;
    ESP_GOTO_ON_ERROR(uart_param_config(port, &cfg), err, TAG, "uart config");
    ESP_GOTO_ON_ERROR(uart_set_pin(port, tx ? pin : UART_PIN_NO_CHANGE, tx ? UART_PIN_NO_CHANGE : pin,
                                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                      err, TAG, "uart pin");
    if (invert) {
        ESP_GOTO_ON_ERROR(uart_set_line_inverse(port, tx ? UART_SIGNAL_TXD_INV : UART_SIGNAL_RXD_INV), err, TAG,
                          "uart invert");
    }
    return ESP_OK;

err:
    uart_driver_delete(port);
    return ret;
}

esp_err_t toslink_task_create(TaskFunction_t fn, const char *name, int stack, void *arg, int priority, int core)
{
    const BaseType_t ok = xTaskCreatePinnedToCore(fn, name, (uint32_t)stack, arg, (UBaseType_t)priority, NULL,
                                                  core < 0 ? tskNO_AFFINITY : (BaseType_t)core);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
