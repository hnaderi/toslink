# toslink

[![CI](https://github.com/hnaderi/toslink/actions/workflows/ci.yml/badge.svg)](https://github.com/hnaderi/toslink/actions/workflows/ci.yml)

One-way message link between ESP32 chips over Toslink optical modules (TOTX/TORX), for ESP-IDF 5.5+.

- Continuous, DC-balanced 1 Mbaud UART stream, as the modules require
- CRC-checked messages of a command byte plus up to 255 payload bytes
- Lost-frame counting and link up/down detection on the receiver

## Install

```
idf.py add-dependency "hnaderi/toslink"
```

## Usage

```c
#include "toslink.h"

/* Transmitter */
toslink_tx_config_t tx_cfg = TOSLINK_TX_DEFAULT_CONFIG(GPIO_NUM_4);
toslink_tx_handle_t tx;
ESP_ERROR_CHECK(toslink_tx_new(&tx_cfg, &tx));

const uint8_t data[] = {0xAB, 0xCD};
ESP_ERROR_CHECK(toslink_tx_send(tx, 0x10, data, sizeof(data), portMAX_DELAY));

/* Receiver */
static void on_message(const toslink_msg_t *msg, void *ctx) { /* msg->cmd, msg->len, msg->payload */ }
static void on_link(bool up, void *ctx) { /* stream started or stopped */ }

toslink_rx_config_t rx_cfg = TOSLINK_RX_DEFAULT_CONFIG(GPIO_NUM_5, on_message);
rx_cfg.on_link = on_link;
toslink_rx_handle_t rx;
ESP_ERROR_CHECK(toslink_rx_new(&rx_cfg, &rx));
```

Each end uses one HP UART (`uart_port`, default UART1) and one task. Callbacks run in the receiver task.

Delivery is best effort: there are no acknowledgements or retransmissions on a simplex link. `toslink_rx_get_stats()` counts lost frames (from sequence gaps) and errors. Don't rely on delivery for critical commands. This kind of link is useful for receiving regular updates or idempotent commands.

The link is up after 32 consecutive valid symbols. It goes down after `link_timeout_ms` (default 20 ms) without them; for instance: fibre unplugged, transmitter off or reset, or a noisy signal. `toslink_rx_link_up()` returns the current state.

## Configuration

`idf.py menuconfig` -> *Toslink*. Both ends must match.

| Option | Default | |
|---|---|---|
| `TOSLINK_MAX_PAYLOAD` | 16 | 1-255 bytes. Each queued TX message uses about this much RAM. |

Throughput with 16-byte payloads: 43 kB/s.

## Wire format

8N1 UART at 1 Mbaud. Every byte on the wire is a symbol with an even number of 1-bits, so a single bit error always yields an invalid symbol. Each byte is 30-70 % high (about 50 % on average), and data pulses last at most 3 µs.

```
..IDLE IDLE [PRE x8] [SOF] [LEN SEQ CMD PAYLOAD.. CRC16] IDLE..
```

- `IDLE` (0x55) fills the line between frames.
- `PRE` (0xF0) has one falling edge per byte. It forces a misaligned receiving UART back onto the byte boundaries.
- `SOF` (0x2B) starts a frame.
- The frame bytes are sent as a bit stream, 6 bits per data symbol. The CRC is CRC-16/CCITT-FALSE.

## Constraints

- **TX task deadline: about 100 µs.** `uart_write_bytes()` without a TX ring buffer waits for the FIFO to drop to 10 bytes. If the task isn't scheduled within about 100 µs after that, the line pauses. Keep `task_priority` high, and pin the task away from busy cores.
- **Flash writes pause the stream.** Erasing or writing flash on the transmitter (NVS, OTA) stops the line for milliseconds. The receiver reports link down, resynchronises at the next preamble, and may lose frames sent meanwhile.
- **Keep callbacks short.** The receive buffer holds about 40 ms of line data.

## Example

[`examples/link_test`](examples/link_test): flash one board as transmitter and one as receiver (menuconfig -> *Toslink link test*). The receiver prints throughput, error counters and link changes. Enable *Soak test* on the transmitter for long error-rate runs.

Wiring: TX GPIO -> TOTX input, TORX output -> RX GPIO, both modules on 3.3 V.

On a healthy link, `crc`, `sym`, `lost`, `uart-fe` and `ovf` stay at 0 after start-up. Rising `uart-fe` points to the signal: wiring, level shifting or pulse distortion. Rising `ovf` points to CPU load on the receiver.

## License

MIT
