#include <string.h>
#include "toslink_proto.h"

/*
 * Data symbols, sorted. Each has an even number of 1-bits, and its 8N1 frame
 * never holds a level longer than 3 bit times, including across the start and
 * stop bits and into the next frame. IDLE and SOF follow the same rules but
 * are reserved. 55 are balanced; 9 have 3 or 7 of 10 bits high, split 4/5 so
 * the average stays near 50 %.
 */
static const uint8_t s_enc[64] = {
    0x12, 0x14, 0x17, 0x1B, 0x1D, 0x22, 0x24, 0x27, 0x2D, 0x2E, 0x33, 0x35, 0x36, 0x39, 0x3A, 0x47,
    0x4B, 0x4D, 0x4E, 0x53, 0x56, 0x59, 0x5A, 0x5C, 0x63, 0x65, 0x66, 0x69, 0x6A, 0x6C, 0x71, 0x72,
    0x74, 0x77, 0x8B, 0x8D, 0x8E, 0x93, 0x95, 0x96, 0x99, 0x9A, 0x9C, 0xA3, 0xA5, 0xA6, 0xA9, 0xAA,
    0xAC, 0xB1, 0xB2, 0xB4, 0xB7, 0xC5, 0xC6, 0xC9, 0xCA, 0xCC, 0xD1, 0xD2, 0xD4, 0xD7, 0xDB, 0xDD,
};

_Static_assert(sizeof(s_enc) == (1 << TOSLINK_SYM_BITS), "symbol table size");

int toslink_decode_symbol(uint8_t sym)
{
    int lo = 0, hi = (int)sizeof(s_enc) - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (s_enc[mid] == sym) {
            return mid;
        }
        if (s_enc[mid] < sym) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -1;
}

bool toslink_symbol_valid(uint8_t sym)
{
    return sym == TOSLINK_SYM_IDLE || sym == TOSLINK_SYM_PRE || sym == TOSLINK_SYM_SOF ||
           toslink_decode_symbol(sym) >= 0;
}

/* unsigned: with a 255 maximum a uint8_t comparison is always true and warns. */
static bool len_ok(unsigned len)
{
    return len <= TOSLINK_MAX_PAYLOAD;
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

size_t toslink_encode_bytes(const uint8_t *raw, size_t n, uint8_t *out)
{
    const unsigned mask = (1u << TOSLINK_SYM_BITS) - 1;
    unsigned acc = 0, nbits = 0;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        acc = (acc << 8) | raw[i];
        nbits += 8;
        while (nbits >= TOSLINK_SYM_BITS) {
            nbits -= TOSLINK_SYM_BITS;
            out[o++] = s_enc[(acc >> nbits) & mask];
        }
    }
    if (nbits) {
        out[o++] = s_enc[(acc << (TOSLINK_SYM_BITS - nbits)) & mask];
    }
    return o;
}

size_t toslink_encode(uint8_t seq, const toslink_msg_t *msg, uint8_t *out)
{
    if (!len_ok(msg->len)) {
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
    out[TOSLINK_PRE_LEN] = TOSLINK_SYM_SOF;
    return TOSLINK_PRE_LEN + 1 + toslink_encode_bytes(raw, r, &out[TOSLINK_PRE_LEN + 1]);
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
            if (sym == TOSLINK_SYM_SOF && p->pre_seen >= TOSLINK_PRE_MIN_SEEN) {
                p->in_frame = true;
                p->acc = 0;
                p->nbits = 0;
                p->got = 0;
                p->total = TOSLINK_MAX_RAW;
            }
            p->pre_seen = 0;
        }
        return false;
    }

    const int val = toslink_decode_symbol(sym);
    if (val < 0) {
        p->stats.symbol_errors++;
        p->in_frame = false;
        p->pre_seen = (sym == TOSLINK_SYM_PRE) ? 1 : 0; /* a new frame may already be starting */
        return false;
    }

    p->acc = (uint16_t)((p->acc << TOSLINK_SYM_BITS) | (unsigned)val);
    p->nbits += TOSLINK_SYM_BITS;
    if (p->nbits < 8) {
        return false;
    }
    p->nbits -= 8;
    p->raw[p->got++] = (uint8_t)(p->acc >> p->nbits);

    if (p->got == 1) {
        if (!len_ok(p->raw[0])) {
            p->stats.symbol_errors++;
            p->in_frame = false;
            return false;
        }
        p->total = (uint16_t)(TOSLINK_HDR_LEN + p->raw[0] + TOSLINK_CRC_LEN);
    }
    if (p->got < p->total) {
        return false;
    }
    p->in_frame = false; /* any remaining bits are padding */
    return finish_frame(p, out);
}

void toslink_link_symbol(toslink_link_t *l, bool valid)
{
    if (!valid) {
        l->run = 0;
    } else if (l->run < TOSLINK_LINK_UP_RUN) {
        l->run++;
    }
    if (l->run == TOSLINK_LINK_UP_RUN) {
        l->healthy = true;
    }
}

toslink_link_event_t toslink_link_poll(toslink_link_t *l, int64_t now_us, int64_t timeout_us)
{
    if (l->healthy) {
        l->healthy = false;
        l->last_healthy_us = now_us;
        if (!l->up) {
            l->up = true;
            return TOSLINK_LINK_UP;
        }
    } else if (l->up && now_us - l->last_healthy_us >= timeout_us) {
        l->up = false;
        return TOSLINK_LINK_DOWN;
    }
    return TOSLINK_LINK_NONE;
}
