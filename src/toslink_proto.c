#include <string.h>
#include "toslink_proto.h"

/*
 * Balanced bytes whose 8N1 frame never holds a level for more than 2 bit
 * times, including across start/stop bits and into the next frame.
 * IDLE and SOF satisfy the same rule but are reserved.
 */
static const uint8_t s_enc[16] = {
    0x2D, 0x33, 0x35, 0x36, 0x4B, 0x4D, 0x53, 0x56, 0x59, 0x5A, 0x65, 0x66, 0x69, 0x6A, 0x93, 0x95,
};

int toslink_decode_symbol(uint8_t sym)
{
    for (int i = 0; i < 16; i++) {
        if (s_enc[i] == sym) {
            return i;
        }
    }
    return -1;
}

uint16_t toslink_crc16(const uint8_t *data, size_t len)
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

size_t toslink_encode(uint8_t seq, const toslink_msg_t *msg, uint8_t *out)
{
    if (msg->len > TOSLINK_MAX_PAYLOAD) {
        return 0;
    }

    uint8_t raw[TOSLINK_MAX_RAW] = {msg->len, seq, msg->cmd};
    size_t r = TOSLINK_HDR_LEN;
    memcpy(&raw[r], msg->payload, msg->len);
    r += msg->len;
    const uint16_t crc = toslink_crc16(raw, r);
    raw[r++] = (uint8_t)(crc >> 8);
    raw[r++] = (uint8_t)crc;

    memset(out, TOSLINK_SYM_PRE, TOSLINK_PRE_LEN);
    size_t o = TOSLINK_PRE_LEN;
    out[o++] = TOSLINK_SYM_SOF;
    for (size_t i = 0; i < r; i++) {
        out[o++] = s_enc[raw[i] >> 4];
        out[o++] = s_enc[raw[i] & 0x0F];
    }
    return o;
}

void toslink_parser_reset(toslink_parser_t *p)
{
    p->in_frame = false;
    p->pre_seen = 0;
}

static bool finish_frame(toslink_parser_t *p, toslink_msg_t *out)
{
    const size_t body = TOSLINK_HDR_LEN + p->raw[0];
    const uint16_t crc = (uint16_t)((p->raw[body] << 8) | p->raw[body + 1]);
    if (toslink_crc16(p->raw, body) != crc) {
        p->stats.crc_errors++;
        return false;
    }

    const uint8_t seq = p->raw[1];
    if (p->have_seq) {
        p->stats.frames_lost += (uint8_t)(seq - p->next_seq); /* gaps over 255 frames are undercounted */
    }
    p->have_seq = true;
    p->next_seq = (uint8_t)(seq + 1);
    p->stats.frames_ok++;

    out->len = p->raw[0];
    out->cmd = p->raw[2];
    memcpy(out->payload, &p->raw[TOSLINK_HDR_LEN], out->len);
    return true;
}

bool toslink_parser_feed(toslink_parser_t *p, uint8_t sym, toslink_msg_t *out)
{
    if (!p->in_frame) {
        if (sym == TOSLINK_SYM_PRE) {
            if (p->pre_seen < TOSLINK_PRE_MIN_SEEN) {
                p->pre_seen++;
            }
        } else {
            p->in_frame = (sym == TOSLINK_SYM_SOF && p->pre_seen >= TOSLINK_PRE_MIN_SEEN);
            p->nsym = 0;
            p->pre_seen = 0;
        }
        return false;
    }

    const int nib = toslink_decode_symbol(sym);
    if (nib < 0) {
        p->stats.symbol_errors++;
        p->in_frame = false;
        p->pre_seen = (sym == TOSLINK_SYM_PRE) ? 1 : 0; /* a new frame may already be starting */
        return false;
    }

    uint8_t *b = &p->raw[p->nsym / 2];
    *b = (p->nsym % 2) ? (uint8_t)(*b | nib) : (uint8_t)(nib << 4);
    p->nsym++;
    if (p->nsym % 2) {
        return false;
    }

    const int got = p->nsym / 2;
    if (p->raw[0] > TOSLINK_MAX_PAYLOAD) {
        p->stats.symbol_errors++;
        p->in_frame = false;
        return false;
    }
    if (got < TOSLINK_HDR_LEN + p->raw[0] + TOSLINK_CRC_LEN) {
        return false;
    }
    p->in_frame = false;
    return finish_frame(p, out);
}
