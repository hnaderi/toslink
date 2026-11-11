#include <string.h>
#include "toslink_proto.h"
#include "unity.h"

static toslink_parser_t s_p;
static toslink_msg_t s_last;
static int s_got;

void setUp(void)
{
    memset(&s_p, 0, sizeof(s_p));
    memset(&s_last, 0, sizeof(s_last));
    s_got = 0;
}

void tearDown(void) {}

static void feed(const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        toslink_msg_t m;
        if (toslink_parser_feed(&s_p, b[i], &m)) {
            s_last = m;
            s_got++;
        }
    }
}

static void idle(int n)
{
    const uint8_t sym = TOSLINK_SYM_IDLE;
    for (int i = 0; i < n; i++) {
        feed(&sym, 1);
    }
}

static toslink_msg_t make_msg(uint8_t cmd, uint8_t len, uint8_t fill)
{
    toslink_msg_t m = {.cmd = cmd, .len = len};
    for (int i = 0; i < len; i++) {
        m.payload[i] = (uint8_t)(fill + i * 31);
    }
    return m;
}

static void assert_msg_equal(const toslink_msg_t *want, const toslink_msg_t *got)
{
    TEST_ASSERT_EQUAL_UINT8(want->cmd, got->cmd);
    TEST_ASSERT_EQUAL_UINT8(want->len, got->len);
    if (want->len) {
        TEST_ASSERT_EQUAL_UINT8_ARRAY(want->payload, got->payload, want->len);
    }
}

/* ---- line coding ---- */

static int popcount8(uint8_t v)
{
    int n = 0;
    for (; v; v &= (uint8_t)(v - 1)) {
        n++;
    }
    return n;
}

/* Appends the 8N1 frame of b (LSB first) to bits; returns the new length. */
static size_t uart_bits(uint8_t b, uint8_t *bits, size_t n)
{
    bits[n++] = 0;
    for (int i = 0; i < 8; i++) {
        bits[n++] = (b >> i) & 1;
    }
    bits[n++] = 1;
    return n;
}

static int max_run(const uint8_t *bits, size_t n)
{
    int best = 0, run = 0;
    for (size_t i = 0; i < n; i++) {
        run = (i > 0 && bits[i] == bits[i - 1]) ? run + 1 : 1;
        best = run > best ? run : best;
    }
    return best;
}

static void test_symbols_are_balanced_short_runs_and_unique(void)
{
    uint8_t line[18];
    int data_symbols = 0;
    for (int v = 0; v < 256; v++) {
        const int nib = toslink_decode_symbol((uint8_t)v);
        if (nib < 0) {
            continue;
        }
        data_symbols++;
        TEST_ASSERT_EQUAL_INT(4, popcount8((uint8_t)v));
        TEST_ASSERT_LESS_OR_EQUAL_INT(2, max_run(line, uart_bits((uint8_t)v, line, 0)));
    }
    TEST_ASSERT_EQUAL_INT(16, data_symbols);
    TEST_ASSERT_EQUAL_INT(-1, toslink_decode_symbol(TOSLINK_SYM_IDLE));
    TEST_ASSERT_EQUAL_INT(-1, toslink_decode_symbol(TOSLINK_SYM_SOF));
    TEST_ASSERT_EQUAL_INT(-1, toslink_decode_symbol(TOSLINK_SYM_PRE));
}

static void test_any_pair_of_non_preamble_symbols_keeps_runs_within_two_bits(void)
{
    uint8_t syms[18];
    int n = 0;
    for (int v = 0; v < 256; v++) {
        if (toslink_decode_symbol((uint8_t)v) >= 0) {
            syms[n++] = (uint8_t)v;
        }
    }
    syms[n++] = TOSLINK_SYM_IDLE;
    syms[n++] = TOSLINK_SYM_SOF;

    uint8_t bits[20];
    for (int a = 0; a < n; a++) {
        for (int b = 0; b < n; b++) {
            const size_t len = uart_bits(syms[b], bits, uart_bits(syms[a], bits, 0));
            TEST_ASSERT_LESS_OR_EQUAL_INT(2, max_run(bits, len));
        }
    }
}

static void test_single_bit_flip_never_yields_a_data_symbol(void)
{
    for (int v = 0; v < 256; v++) {
        if (toslink_decode_symbol((uint8_t)v) < 0) {
            continue;
        }
        for (int bit = 0; bit < 8; bit++) {
            TEST_ASSERT_EQUAL_INT(-1, toslink_decode_symbol((uint8_t)(v ^ (1 << bit))));
        }
    }
}

static void test_crc_matches_reference_vector(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x29B1, toslink_crc16((const uint8_t *)"123456789", 9));
}

static void test_encode_layout_and_length_limit(void)
{
    uint8_t out[TOSLINK_MAX_WIRE];
    toslink_msg_t m = make_msg(1, TOSLINK_MAX_PAYLOAD, 0);
    TEST_ASSERT_EQUAL_size_t(TOSLINK_MAX_WIRE, toslink_encode(0, &m, out));
    for (int i = 0; i < TOSLINK_PRE_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(TOSLINK_SYM_PRE, out[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(TOSLINK_SYM_SOF, out[TOSLINK_PRE_LEN]);

    m.len = TOSLINK_MAX_PAYLOAD + 1;
    TEST_ASSERT_EQUAL_size_t(0, toslink_encode(0, &m, out));
}

/* ---- parser ---- */

static void test_round_trip_all_lengths_and_seqs(void)
{
    uint8_t buf[TOSLINK_MAX_WIRE];
    for (int s = 0; s < 512; s++) {
        const toslink_msg_t m = make_msg((uint8_t)(255 - s), (uint8_t)(s % (TOSLINK_MAX_PAYLOAD + 1)), (uint8_t)s);
        feed(buf, toslink_encode((uint8_t)s, &m, buf));
        idle(3);
        TEST_ASSERT_EQUAL_INT(s + 1, s_got);
        assert_msg_equal(&m, &s_last);
    }
    TEST_ASSERT_EQUAL_UINT32(0, s_p.stats.frames_lost);
}

static void test_idle_stream_yields_nothing(void)
{
    idle(1000);
    TEST_ASSERT_EQUAL_INT(0, s_got);
    TEST_ASSERT_EQUAL_UINT32(0, s_p.stats.symbol_errors);
}

static void test_every_single_bit_error_in_frame_is_rejected(void)
{
    uint8_t buf[TOSLINK_MAX_WIRE], bad[TOSLINK_MAX_WIRE];
    const toslink_msg_t m = make_msg(0x42, 5, 1);
    const size_t n = toslink_encode(7, &m, buf);
    for (size_t pos = TOSLINK_PRE_LEN; pos < n; pos++) {
        for (int bit = 0; bit < 8; bit++) {
            memcpy(bad, buf, n);
            bad[pos] ^= (uint8_t)(1 << bit);
            feed(bad, n);
            idle(5);
        }
    }
    TEST_ASSERT_EQUAL_INT(0, s_got);
}

static void test_resyncs_after_truncated_frame(void)
{
    uint8_t buf[TOSLINK_MAX_WIRE];
    const uint8_t junk[] = {0x00, 0xFF, 0x12};
    const toslink_msg_t m = make_msg(1, 0, 0);
    const size_t n = toslink_encode(8, &m, buf);
    feed(buf, n - 4);
    feed(junk, sizeof(junk));
    feed(buf, n);
    idle(4);
    TEST_ASSERT_EQUAL_INT(1, s_got);
    TEST_ASSERT_EQUAL_UINT32(1, s_p.stats.symbol_errors);
    assert_msg_equal(&m, &s_last);
}

static void test_frame_cut_by_next_preamble_still_delivers_next(void)
{
    uint8_t a[TOSLINK_MAX_WIRE], b[TOSLINK_MAX_WIRE];
    const toslink_msg_t ma = make_msg(1, 10, 1), mb = make_msg(2, 3, 2);
    const size_t na = toslink_encode(0, &ma, a);
    const size_t nb = toslink_encode(1, &mb, b);
    feed(a, na - 6);
    feed(b, nb);
    TEST_ASSERT_EQUAL_INT(1, s_got);
    assert_msg_equal(&mb, &s_last);
}

static void test_sof_needs_preamble(void)
{
    uint8_t buf[TOSLINK_MAX_WIRE];
    const toslink_msg_t m = make_msg(1, 2, 3);
    const size_t n = toslink_encode(9, &m, buf);
    const size_t skip = TOSLINK_PRE_LEN - (TOSLINK_PRE_MIN_SEEN - 1);
    idle(3);
    feed(buf + skip, n - skip);
    TEST_ASSERT_EQUAL_INT(0, s_got);

    feed(buf + skip - 1, n - skip + 1);
    TEST_ASSERT_EQUAL_INT(1, s_got);
}

static void test_oversize_length_field_is_rejected(void)
{
    uint8_t buf[TOSLINK_MAX_WIRE];
    const toslink_msg_t m = make_msg(1, 0, 0);
    toslink_encode(0, &m, buf);
    const uint8_t one = 0x33; /* symbol for nibble 1, so LEN = 0x11 */
    TEST_ASSERT_EQUAL_INT(1, toslink_decode_symbol(one));
    buf[TOSLINK_PRE_LEN + 1] = one;
    buf[TOSLINK_PRE_LEN + 2] = one;
    feed(buf, TOSLINK_PRE_LEN + 3);
    TEST_ASSERT_EQUAL_UINT32(1, s_p.stats.symbol_errors);
    TEST_ASSERT_FALSE(s_p.in_frame);
}

static void test_seq_gaps_count_as_lost(void)
{
    uint8_t buf[TOSLINK_MAX_WIRE];
    const toslink_msg_t m = make_msg(1, 1, 1);
    const uint8_t seqs[] = {250, 251, 254, 255, 3};
    for (size_t i = 0; i < sizeof(seqs); i++) {
        feed(buf, toslink_encode(seqs[i], &m, buf));
    }
    TEST_ASSERT_EQUAL_INT(5, s_got);
    TEST_ASSERT_EQUAL_UINT32(2 + 3, s_p.stats.frames_lost);
}

static void test_reset_drops_partial_frame(void)
{
    uint8_t buf[TOSLINK_MAX_WIRE];
    const toslink_msg_t m = make_msg(1, 8, 1);
    const size_t n = toslink_encode(0, &m, buf);
    feed(buf, n / 2);
    toslink_parser_reset(&s_p);
    feed(buf + n / 2, n - n / 2);
    TEST_ASSERT_EQUAL_INT(0, s_got);
}

/* ---- bit-level UART receiver model ---- */

/*
 * Decodes a bit stream like a UART receiver that starts at bit `offset`.
 * A start bit is a 1->0 transition; after a framing error the receiver either
 * waits for the line to go high first (edge_after_error) or treats a low
 * stop-bit sample as the next start bit.
 */
static size_t uart_receive(const uint8_t *bits, size_t nbits, size_t offset, bool edge_after_error, uint8_t *out)
{
    size_t n = 0, i = offset + 1;
    while (i + 9 < nbits) {
        if (!(bits[i] == 0 && bits[i - 1] == 1)) {
            i++;
            continue;
        }
        uint8_t b = 0;
        for (int k = 0; k < 8; k++) {
            b |= (uint8_t)(bits[i + 1 + k] << k);
        }
        if (bits[i + 9] == 1) {
            out[n++] = b;
            i += 10;
        } else if (edge_after_error) {
            i += 10;
        } else {
            i += 9;
            if (i + 9 < nbits) { /* low stop sample acts as a start bit */
                b = 0;
                for (int k = 0; k < 8; k++) {
                    b |= (uint8_t)(bits[i + 1 + k] << k);
                }
                if (bits[i + 9] == 1) {
                    out[n++] = b;
                }
                i += 10;
            }
        }
    }
    return n;
}

static void test_receiver_starting_at_any_bit_offset_gets_the_frame(void)
{
    static uint8_t bytes[64 + TOSLINK_MAX_WIRE + 16];
    static uint8_t bits[sizeof(bytes) * 10];
    static uint8_t rx[sizeof(bytes)];

    const toslink_msg_t m = make_msg(0x5A, TOSLINK_MAX_PAYLOAD, 9);
    size_t nb = 0;
    memset(bytes, TOSLINK_SYM_IDLE, 64);
    nb += 64;
    nb += toslink_encode(3, &m, &bytes[nb]);
    memset(&bytes[nb], TOSLINK_SYM_IDLE, 16);
    nb += 16;

    size_t nbits = 0;
    for (size_t i = 0; i < nb; i++) {
        nbits = uart_bits(bytes[i], bits, nbits);
    }

    for (int mode = 0; mode < 2; mode++) {
        for (size_t off = 0; off < 300; off++) {
            setUp();
            feed(rx, uart_receive(bits, nbits, off, mode == 0, rx));
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_got, mode ? "level after error" : "edge after error");
            assert_msg_equal(&m, &s_last);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_symbols_are_balanced_short_runs_and_unique);
    RUN_TEST(test_any_pair_of_non_preamble_symbols_keeps_runs_within_two_bits);
    RUN_TEST(test_single_bit_flip_never_yields_a_data_symbol);
    RUN_TEST(test_crc_matches_reference_vector);
    RUN_TEST(test_encode_layout_and_length_limit);
    RUN_TEST(test_round_trip_all_lengths_and_seqs);
    RUN_TEST(test_idle_stream_yields_nothing);
    RUN_TEST(test_every_single_bit_error_in_frame_is_rejected);
    RUN_TEST(test_resyncs_after_truncated_frame);
    RUN_TEST(test_frame_cut_by_next_preamble_still_delivers_next);
    RUN_TEST(test_sof_needs_preamble);
    RUN_TEST(test_oversize_length_field_is_rejected);
    RUN_TEST(test_seq_gaps_count_as_lost);
    RUN_TEST(test_reset_drops_partial_frame);
    RUN_TEST(test_receiver_starting_at_any_bit_offset_gets_the_frame);
    return UNITY_END();
}
