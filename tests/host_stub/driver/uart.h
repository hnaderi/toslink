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
typedef enum { UART_DATA, UART_BREAK, UART_BUFFER_FULL, UART_FIFO_OVF, UART_FRAME_ERR, UART_PARITY_ERR } uart_event_type_t;
typedef struct { uart_event_type_t type; size_t size; } uart_event_t;
static inline int uart_set_rx_full_threshold(uart_port_t p, int t) { (void)p; (void)t; return 0; }
static inline int uart_flush_input(uart_port_t p) { (void)p; return 0; }
static inline int uart_param_config(uart_port_t p, const uart_config_t *c) { (void)p;(void)c; return 0; }
static inline int uart_set_pin(uart_port_t p, int a, int b, int c, int d) { (void)p;(void)a;(void)b;(void)c;(void)d; return 0; }
#define UART_SIGNAL_RXD_INV (1 << 2)
static inline int uart_set_line_inverse(uart_port_t p, uint32_t m) { (void)p; (void)m; return 0; }
