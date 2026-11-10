#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
int64_t g_now = 0;
#include "../components/toslink/toslink_rx.c"

static int got_count; static tl_msg_t last;
static void cb(const tl_msg_t *m, void *c) { (void)c; got_count++; last = *m; }

static void feed(const uint8_t *b, size_t n) { for (size_t i = 0; i < n; i++) parse(b[i]); }
static void idle(int n) { for (int i = 0; i < n; i++) parse(TL_SYM_IDLE); }
static size_t make(const tl_msg_t *m, uint8_t *out) {
    size_t n = 0;
    for (int i = 0; i < TL_PRE_LEN; i++) out[n++] = TL_SYM_PRE;
    n += tl_encode(m, out + n);
    return n;
}

int main(void) {
    s_rx.cfg.cb = cb; s_rx.cfg.dup_window_ms = 200;
    uint8_t buf[256]; size_t n;
    tl_msg_t m = { .seq = 7, .cmd = 0x42, .len = 5, .payload = {1,2,3,4,5} };

    /* 1. clean frame */
    idle(50); n = make(&m, buf); feed(buf, n); idle(10);
    assert(got_count == 1 && last.cmd == 0x42 && last.len == 5 && last.payload[4] == 5);

    /* 2. two repeats with same seq -> filtered */
    g_now += 3000; feed(buf, n); idle(10); g_now += 3000; feed(buf, n); idle(10);
    assert(got_count == 1 && s_rx.stats.duplicates == 2);

    /* 3. every single-bit error anywhere in the frame body is rejected */
    int delivered_bad = 0;
    for (size_t pos = TL_PRE_LEN + 1; pos < n; pos++)
        for (int bit = 0; bit < 8; bit++) {
            uint8_t c[256]; memcpy(c, buf, n); c[pos] ^= (uint8_t)(1 << bit);
            tl_msg_t m2 = m; m2.seq = (uint8_t)(100 + pos); /* unique seq: no dup filtering */
            (void)m2;
            int before = got_count; g_now += 1000000; feed(c, n); idle(5);
            if (got_count != before) delivered_bad++;
        }
    assert(delivered_bad == 0);

    /* 4. frame truncated by garbage, then a good frame right after -> resyncs */
    uint8_t junk[] = { 0x00, 0xFF, 0x12 };
    tl_msg_t m3 = { .seq = 8, .cmd = 1, .len = 0 };
    n = make(&m3, buf); feed(buf, n - 4); feed(junk, 3); n = make(&m3, buf); feed(buf, n); idle(4);
    assert(got_count == 2 && last.seq == 8 && last.len == 0);

    /* 5. SOF without enough preamble is ignored */
    m3.seq = 9; n = tl_encode(&m3, buf); idle(3); parse(TL_SYM_PRE); feed(buf, n); idle(4);
    assert(got_count == 2);

    /* 6. max payload + every seq value round-trips */
    for (int s = 0; s < 256; s++) {
        tl_msg_t mm = { .seq = (uint8_t)s, .cmd = (uint8_t)(255 - s), .len = TL_MAX_PAYLOAD };
        for (int i = 0; i < TL_MAX_PAYLOAD; i++) mm.payload[i] = (uint8_t)(s * 31 + i);
        int before = got_count; n = make(&mm, buf); g_now += 1000; feed(buf, n); idle(3);
        assert(got_count == before + 1 && memcmp(last.payload, mm.payload, TL_MAX_PAYLOAD) == 0);
    }
    /* 7. oversize payload refused by encoder */
    tl_msg_t big = { .len = TL_MAX_PAYLOAD + 1 }; assert(tl_encode(&big, buf) == 0);

    printf("all tests passed: ok=%u dup=%u crc=%u sym=%u (single-bit errors injected, none delivered)\n",
           s_rx.stats.frames_ok, s_rx.stats.duplicates, s_rx.stats.crc_errors, s_rx.stats.symbol_errors);
    return 0;
}
