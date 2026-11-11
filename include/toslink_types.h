#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOSLINK_MAX_PAYLOAD 16

typedef struct {
    uint8_t cmd;
    uint8_t len; /* 0..TOSLINK_MAX_PAYLOAD */
    uint8_t payload[TOSLINK_MAX_PAYLOAD];
} toslink_msg_t;

typedef struct {
    uint32_t frames_ok;
    uint32_t frames_lost; /* from seq gaps; a transmitter restart adds one bogus gap */
    uint32_t crc_errors;
    uint32_t symbol_errors;     /* invalid symbol or length inside a frame */
    uint32_t uart_frame_errors; /* bad stop bit or break: signal-level problem */
    uint32_t uart_overflows;    /* RX FIFO or ring buffer overflowed: bytes were dropped */
} toslink_rx_stats_t;

#ifdef __cplusplus
}
#endif
