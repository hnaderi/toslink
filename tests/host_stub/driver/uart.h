#pragma once
#include <stdint.h>
typedef int uart_port_t;
#define UART_NUM_1 1
#define SOC_UART_HP_NUM 3
#define UART_PIN_NO_CHANGE (-1)
typedef struct { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; } uart_config_t;
#define UART_DATA_8_BITS 3
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
static inline int uart_read_bytes(uart_port_t p, void *b, uint32_t l, uint32_t t) { (void)p;(void)b;(void)l;(void)t; return 0; }
#include <stddef.h>
static inline int uart_driver_install(uart_port_t p, int a, int b, int c, void *q, int f) { (void)p;(void)a;(void)b;(void)c;(void)q;(void)f; return 0; }
static inline int uart_param_config(uart_port_t p, const uart_config_t *c) { (void)p;(void)c; return 0; }
static inline int uart_set_pin(uart_port_t p, int a, int b, int c, int d) { (void)p;(void)a;(void)b;(void)c;(void)d; return 0; }
