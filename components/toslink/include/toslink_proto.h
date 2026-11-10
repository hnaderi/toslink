/*
 * Toslink command link - line coding and framing shared by TX and RX.
 *
 * Physical layer: plain 8N1 UART at TL_BAUD, but only "balanced" byte values
 * are ever sent. Every symbol below has four 1-bits, so together with the
 * start (0) and stop (1) bit each 10-bit UART frame is exactly 50 % high, and
 * no level lasts longer than 2 bit times (data symbols) or 5 bit times
 * (preamble). A single flipped bit always breaks the balance, so it can never
 * turn one valid symbol into another.
 *
 * Wire format of one message:
 *
 *   ..IDLE IDLE [PRE x TL_PRE_LEN] [SOF] [LEN SEQ CMD PAYLOAD.. CRC_HI CRC_LO]* IDLE IDLE..
 *                                        * each byte sent as two nibble symbols, high nibble first
 *
 *   IDLE (0x55) : sent continuously between messages, so the line never rests.
 *   PRE  (0xF0) : 5 low + 5 high bit times. It has only ONE falling edge per
 *                 UART frame, so a receiver that is mis-framed (after power-up
 *                 or a disturbance) is forced back onto the correct byte grid
 *                 within a few bytes. (0x55 alone cannot do this: every falling
 *                 edge in it looks like a valid start bit.)
 *   SOF  (0x2B) : start of frame; never used as a data symbol.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TL_BAUD          1000000                  /* 1 Mbaud: longest pulse 5 us */
#define TL_BYTE_US       (10 * 1000000 / TL_BAUD) /* one 8N1 frame = 10 us */

#define TL_SYM_IDLE      0x55
#define TL_SYM_PRE       0xF0
#define TL_SYM_SOF       0x2B

#define TL_PRE_LEN       16   /* 160 us of realignment before every frame */
#define TL_PRE_MIN_SEEN  2    /* RX accepts SOF only after this many PRE symbols */

#define TL_MAX_PAYLOAD   16
#define TL_HDR_LEN       3    /* LEN, SEQ, CMD */
#define TL_CRC_LEN       2
#define TL_MAX_RAW       (TL_HDR_LEN + TL_MAX_PAYLOAD + TL_CRC_LEN)
#define TL_MAX_ENCODED   (1 + 2 * TL_MAX_RAW) /* SOF + two symbols per byte */

typedef struct {
    uint8_t seq;                     /* all repeats of one command share a seq */
    uint8_t cmd;
    uint8_t len;                     /* payload length, 0..TL_MAX_PAYLOAD */
    uint8_t payload[TL_MAX_PAYLOAD];
} tl_msg_t;

/* Writes SOF + symbols into out (>= TL_MAX_ENCODED bytes). Returns bytes written, 0 if msg->len is too big. */
size_t tl_encode(const tl_msg_t *msg, uint8_t *out);

/* Returns the nibble (0..15) a received symbol stands for, or -1 if it is not a data symbol. */
int tl_decode_symbol(uint8_t sym);

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). */
uint16_t tl_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
