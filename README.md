# toslink

[![CI](https://github.com/hnaderi/toslink/actions/workflows/ci.yml/badge.svg)](https://github.com/hnaderi/toslink/actions/workflows/ci.yml)

One-way message link between ESP32 chips over Toslink optical modules
(e.g. TOTX147 / TORX147), for ESP-IDF 6.0+. The optical signal stays
DC-balanced and toggles continuously, as those modules require.

## Installation

```
idf.py add-dependency "hnaderi/toslink"
```

## Usage

```c
#include "toslink.h"

/* transmitter */
toslink_tx_config_t tx_cfg = TOSLINK_TX_DEFAULT_CONFIG(GPIO_NUM_4);
toslink_tx_handle_t tx;
ESP_ERROR_CHECK(toslink_tx_new(&tx_cfg, &tx));
const uint8_t data[] = {0xAB, 0xCD};
ESP_ERROR_CHECK(toslink_tx_send(tx, 0x10, data, sizeof(data), portMAX_DELAY));

/* receiver */
static void on_msg(const toslink_msg_t *msg, void *ctx) { /* runs in the RX task */ }

toslink_rx_config_t rx_cfg = TOSLINK_RX_DEFAULT_CONFIG(GPIO_NUM_5, on_msg);
toslink_rx_handle_t rx;
ESP_ERROR_CHECK(toslink_rx_new(&rx_cfg, &rx));
```

A message is a command byte plus up to 16 payload bytes. Each link uses one
HP UART (UART1 by default, set `uart_port`) and one task.

Delivery is best effort: there is no acknowledgement and no retransmission.
Frames that fail the CRC or line-coding checks are dropped, and every frame
carries a sequence number, so `toslink_rx_get_stats()` reports dropped frames
in `frames_lost`. If a command must arrive, design it to be idempotent and
send it again.

### Inverted signals

Set `.invert = true` when a level shifter in the path inverts the signal. TX
and RX settings are independent. What matters is that the receiving UART sees
the original polarity. Inversion does not affect the line coding.

## Example

[`examples/link_test`](examples/link_test) sends text lines, then saturates
the link and reports throughput and error counters. Flash one board as
transmitter and one as receiver (`idf.py menuconfig` → *Toslink link test*).
Wiring: TX GPIO → TOTX data input, TORX data output → RX GPIO, both modules on
3.3 V with the decoupling their datasheets ask for.

## How it works

* **Line coding.** 8N1 UART at 1 Mbaud. Only bytes with exactly four 1-bits
  are sent, so every UART frame is 50 % high. Data symbols never hold a level
  longer than 2 µs, and the preamble never longer than 5 µs. A single bit error
  always breaks the balance, so it is caught even before the CRC check.
* **Continuous stream.** The UART owns the pin permanently. A task keeps its
  FIFO topped up with IDLE symbols (`0x55`), or with a frame when one is
  queued. The line never rests and never switches signal source.
* **Alignment.** Each frame starts with 16 × `0xF0`, which has a single
  falling edge per byte. That forces the receiving UART back onto the correct
  byte boundary after power-up or any disturbance.
* **Frame.** `[0xF0 ×16] [SOF 0x2B] [LEN SEQ CMD PAYLOAD.. CRC16]`. Each byte is
  sent as two nibble symbols, and the CRC is CRC-16/CCITT-FALSE. A full frame
  is 59 bytes on the wire, so payload throughput is about 27 kB/s.
* **Receiver.** The RX FIFO threshold is lowered to 64 bytes. At 1 Mbaud that
  gives the interrupt about 640 µs of slack, where the driver default leaves
  80 µs. UART framing errors and overflows are counted.

## Timing constraints

* **TX task deadline: ~100 µs.** With no TX ring buffer, `uart_write_bytes()`
  re-arms the FIFO-empty interrupt at a fixed 10-byte threshold. The TX task
  must therefore run within about 100 µs of that interrupt, or the line pauses.
  Keep `task_priority` high (default 15), and pin the task to a core that is
  not busy with Wi-Fi if needed.
* **Flash operations stall the stream.** Erasing or writing flash (NVS, OTA)
  suspends tasks and non-IRAM interrupts for milliseconds. The line stops
  toggling for that time. The receiver recovers at the next preamble, but
  frames sent during the stall can be lost.
* **The RX callback runs in the receiver task.** Reception stalls while it
  runs, and the 4 KB ring buffer covers about 40 ms. Hand longer work off to
  another task.

## Verifying on hardware

* Scope the TX pin. At idle you should see a continuous 500 kHz square wave.
  During a message you should see 5 µs pulses, then data. There should be no
  gaps, glitches or long constant levels anywhere.
* A healthy receiver keeps `uart_frame_errors` and `uart_overflows` at 0 after
  start-up (a few at power-up are normal), and `crc_errors`, `symbol_errors`
  and `frames_lost` near 0.
* Rising `uart_frame_errors` point at the signal: wiring, level shifting or
  pulse distortion. Rising `uart_overflows` point at CPU load: raise the RX
  task priority or move it to another core.

## Development

```
nix develop             # or any shell with ESP-IDF 6.x, cmake and clang-format
test-host               # host unit tests (Unity + CTest)
fw-build esp32s3        # builds examples/link_test into build/esp32s3
format-cpp-check
```

The host tests cover the line-coding invariants, the encoder and the frame
parser. They include single-bit-error injection and a bit-level UART receiver
model that starts at every bit offset.

The repository directory must be named `toslink`. ESP-IDF names a component
after its directory, so the example can only resolve the dependency under
that name.

Releases are published to the ESP Component Registry by pushing a tag `vX.Y.Z`
that matches `version` in `idf_component.yml`.

## License

MIT
