/*
 * Line coding, framing and link monitoring, shared by TX and RX. Pure C, no ESP-IDF dependency.
 *
 * 8N1 UART at TOSLINK_BAUD. Every symbol byte has an even number of 1-bits,
 * so a single flipped bit never turns one valid symbol into another. Each
 * 10-bit UART frame is 30-70 % high, and data symbols never hold a level
 * longer than 3 bit times (the preamble 5).
 *
 *   ..IDLE [PRE x TOSLINK_PRE_LEN] [SOF] [LEN SEQ CMD PAYLOAD.. CRC_HI CRC_LO]* IDLE..
 *                                        * bit stream, MSB first, TOSLINK_SYM_BITS per
 *                                          symbol, last symbol zero-padded
 *
 *   IDLE 0x55: sent between frames so the line never rests.
 *   PRE  0xF0: one falling edge per UART frame, which forces a mis-framed
 *              receiver back onto the byte grid. 0x55 cannot: each of its
 *              falling edges looks like a valid start bit.
 *   SOF  0x2B: never a data symbol.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "toslink_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TOSLINK_BAUD 1000000

#define TOSLINK_SYM_BITS 6 /* data bits per symbol */

#define TOSLINK_SYM_IDLE 0x55
#define TOSLINK_SYM_PRE  0xF0
#define TOSLINK_SYM_SOF  0x2B

#define TOSLINK_PRE_LEN      8 /* resync needs 4 in a bit-level UART model; the rest is margin */
#define TOSLINK_PRE_MIN_SEEN 2 /* PRE symbols required before SOF is accepted */

#define TOSLINK_HDR_LEN  3 /* LEN, SEQ, CMD */
#define TOSLINK_CRC_LEN  2
#define TOSLINK_MAX_RAW  (TOSLINK_HDR_LEN + TOSLINK_MAX_PAYLOAD + TOSLINK_CRC_LEN)
#define TOSLINK_SYMS(n)  ((8 * (n) + TOSLINK_SYM_BITS - 1) / TOSLINK_SYM_BITS)
#define TOSLINK_MAX_WIRE (TOSLINK_PRE_LEN + 1 + TOSLINK_SYMS(TOSLINK_MAX_RAW))

#define TOSLINK_LINK_UP_RUN 32 /* consecutive valid symbols that prove the link is up */

/* Writes preamble, SOF and frame into out (>= TOSLINK_MAX_WIRE bytes). Returns 0 if msg->len is too big. */
size_t toslink_encode(uint8_t seq, const toslink_msg_t *msg, uint8_t *out);

/* Encodes raw bytes as data symbols. Returns the number of symbols written. */
size_t toslink_encode_bytes(const uint8_t *raw, size_t n, uint8_t *out);

/* Value of a data symbol, or -1. */
int toslink_decode_symbol(uint8_t sym);

/* True for any symbol a healthy transmitter sends. */
bool toslink_symbol_valid(uint8_t sym);

/* CRC-16/CCITT-FALSE. */
uint16_t toslink_crc16(const uint8_t *data, size_t len);

/* Zero-initialise before use. */
typedef struct {
    uint8_t pre_seen;
    bool in_frame;
    uint16_t acc;   /* decoded bits not yet assembled into a byte */
    uint8_t nbits;  /* valid bits in acc */
    uint16_t got;   /* bytes received in the current frame */
    uint16_t total; /* frame length in bytes, known once LEN arrives */
    uint8_t raw[TOSLINK_MAX_RAW];
    bool have_seq;
    uint8_t next_seq;
    toslink_rx_stats_t stats; /* uart_* and link_* fields are left to the caller */
} toslink_parser_t;

/* Returns true when sym completes a valid frame, which is then stored in *out. */
bool toslink_parser_feed(toslink_parser_t *p, uint8_t sym, toslink_msg_t *out);

/* Drops any partial frame, e.g. after the UART lost bytes. */
void toslink_parser_reset(toslink_parser_t *p);

typedef enum { TOSLINK_LINK_NONE, TOSLINK_LINK_UP, TOSLINK_LINK_DOWN } toslink_link_event_t;

/* Zero-initialise before use; starts down. */
typedef struct {
    bool up;
    bool healthy; /* a full run was seen since the last poll */
    uint8_t run;
    int64_t last_healthy_us;
} toslink_link_t;

/* Call for every received symbol, and with valid = false for every UART error. */
void toslink_link_symbol(toslink_link_t *l, bool valid);

/* Call periodically, at least twice per timeout. Reports a state change, if any. */
toslink_link_event_t toslink_link_poll(toslink_link_t *l, int64_t now_us, int64_t timeout_us);

#ifdef __cplusplus
}
#endif
