#include <string.h>
#include "toslink_proto.h"

/*
 * The 16 data symbols. Each is a balanced byte (four 1-bits) whose 8N1 frame
 * never holds a level for more than 2 bit times, including across the start
 * and stop bits and into the next frame. 0x55 (IDLE) and 0x2B (SOF) are
 * reserved; 0xF0 (PRE) has 5-bit runs and is not in the set either.
 */
static const uint8_t s_enc[16] = {
    0x2D, 0x33, 0x35, 0x36, 0x4B, 0x4D, 0x53, 0x56,
    0x59, 0x5A, 0x65, 0x66, 0x69, 0x6A, 0x93, 0x95,
};

int tl_decode_symbol(uint8_t sym)
{
    for (int i = 0; i < 16; i++) {
        if (s_enc[i] == sym) {
            return i;
        }
    }
    return -1;
}

uint16_t tl_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t tl_encode(const tl_msg_t *msg, uint8_t *out)
{
    if (msg->len > TL_MAX_PAYLOAD) {
        return 0;
    }

    uint8_t raw[TL_MAX_RAW];
    size_t r = 0;
    raw[r++] = msg->len;
    raw[r++] = msg->seq;
    raw[r++] = msg->cmd;
    memcpy(&raw[r], msg->payload, msg->len);
    r += msg->len;
    const uint16_t crc = tl_crc16(raw, r);
    raw[r++] = (uint8_t)(crc >> 8);
    raw[r++] = (uint8_t)(crc & 0xFF);

    size_t o = 0;
    out[o++] = TL_SYM_SOF;
    for (size_t i = 0; i < r; i++) {
        out[o++] = s_enc[raw[i] >> 4];
        out[o++] = s_enc[raw[i] & 0x0F];
    }
    return o;
}
