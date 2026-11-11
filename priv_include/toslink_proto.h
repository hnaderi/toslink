/*
 * Line coding and framing, shared by TX and RX. Pure C, no ESP-IDF dependency.
 *
 * Physical layer: 8N1 UART at TOSLINK_BAUD sending only balanced bytes (four
 * 1-bits), so every 10-bit UART frame is 50 % high. Data symbols never hold a
 * level longer than 2 bit times, the preamble 5. A single flipped bit always
 * breaks the balance, so it cannot turn one valid symbol into another.
 *
 *   ..IDLE [PRE x TOSLINK_PRE_LEN] [SOF] [LEN SEQ CMD PAYLOAD.. CRC_HI CRC_LO]* IDLE..
 *                                        * each byte as two nibble symbols, high first
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

#define TOSLINK_SYM_IDLE 0x55
#define TOSLINK_SYM_PRE  0xF0
#define TOSLINK_SYM_SOF  0x2B

#define TOSLINK_PRE_LEN      16
#define TOSLINK_PRE_MIN_SEEN 2 /* PRE symbols required before SOF is accepted */

#define TOSLINK_HDR_LEN  3 /* LEN, SEQ, CMD */
#define TOSLINK_CRC_LEN  2
#define TOSLINK_MAX_RAW  (TOSLINK_HDR_LEN + TOSLINK_MAX_PAYLOAD + TOSLINK_CRC_LEN)
#define TOSLINK_MAX_WIRE (TOSLINK_PRE_LEN + 1 + 2 * TOSLINK_MAX_RAW)

/* Writes preamble, SOF and frame into out (>= TOSLINK_MAX_WIRE bytes). Returns 0 if msg->len is too big. */
size_t toslink_encode(uint8_t seq, const toslink_msg_t *msg, uint8_t *out);

/* Nibble a data symbol stands for, or -1. */
int toslink_decode_symbol(uint8_t sym);

/* CRC-16/CCITT-FALSE. */
uint16_t toslink_crc16(const uint8_t *data, size_t len);

/* Zero-initialise before use. */
typedef struct {
    uint8_t pre_seen;
    bool in_frame;
    uint8_t nsym; /* symbols received in the current frame */
    uint8_t raw[TOSLINK_MAX_RAW];
    bool have_seq;
    uint8_t next_seq;
    toslink_rx_stats_t stats; /* uart_* fields are left to the caller */
} toslink_parser_t;

/* Returns true when sym completes a valid frame, which is then stored in *out. */
bool toslink_parser_feed(toslink_parser_t *p, uint8_t sym, toslink_msg_t *out);

/* Drops any partial frame, e.g. after the UART lost bytes. */
void toslink_parser_reset(toslink_parser_t *p);

#ifdef __cplusplus
}
#endif
