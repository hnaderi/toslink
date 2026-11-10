# toslink — one-way command link over Toslink for ESP32 (ESP-IDF 6.0+)

Sends short command messages from one ESP32 to another through a pair of
Toslink modules (e.g. TOTX147 / TORX147), keeping the optical signal
DC-balanced and continuously toggling as the modules require.

## Layout

```
toslink_demo/
├── components/toslink/      <- the reusable component (copy this into your project)
│   ├── include/toslink.h    (umbrella header)
│   ├── include/toslink_proto.h, toslink_tx.h, toslink_rx.h
│   └── toslink_proto.c, toslink_tx.c, toslink_rx.c
└── main/                    <- demo app (TX or RX, chosen in menuconfig)
```

## Build and flash the demo

```
idf.py set-target esp32          # or esp32s3, esp32c3, ...
idf.py menuconfig                # "Toslink demo" -> role + GPIO
idf.py -p PORT flash monitor
```

Flash one board as **Transmitter** and the other as **Receiver**.
To keep two configurations side by side:

```
idf.py -B build_tx -D SDKCONFIG=build_tx/sdkconfig build
idf.py -B build_rx -D SDKCONFIG=build_rx/sdkconfig menuconfig   # select Receiver
idf.py -B build_rx -D SDKCONFIG=build_rx/sdkconfig build
```

Wiring: TX GPIO (default 4) -> TOTX data input. TORX data output -> RX GPIO
(default 5). Both modules on 3.3 V with the decoupling their datasheets ask for.

## Using the component

```c
#include "toslink.h"

/* transmitter */
toslink_tx_config_t tx = TOSLINK_TX_DEFAULT_CONFIG(GPIO_NUM_4);
ESP_ERROR_CHECK(toslink_tx_init(&tx));
tl_msg_t msg = { .seq = seq++, .cmd = 0x10, .len = 2, .payload = { 0xAB, 0xCD } };
for (int i = 0; i < 3; i++) toslink_tx_send(&msg, portMAX_DELAY);   /* repeats share one seq */

/* receiver */
static void on_msg(const tl_msg_t *m, void *ctx) { /* runs in the RX task */ }
toslink_rx_config_t rx = TOSLINK_RX_DEFAULT_CONFIG(GPIO_NUM_5, on_msg);
ESP_ERROR_CHECK(toslink_rx_init(&rx));
```

Defaults use UART1; change `uart_port` in the config struct if your
application already uses it. Each side runs one task: the TX feeder
(priority 15 by default) and the RX parser (priority 10).

## Inverted (active-low) signals

Set `.invert = true` in either config (or the matching menuconfig option in the
demo) when a level shifter in the path inverts the signal.

* **TX / RX:** the UART's own TX / RX line inversion is used.
* The two settings are independent. What matters is that the *receiving UART*
  sees the original polarity: an inverter on the TX side must be cancelled by
  either a second inverter in the RX path or `.invert = true` on the receiver.
* Line coding is unaffected: an inverted balanced frame is still exactly 50 %
  high with the same maximum pulse lengths.

## How it works

* **Line coding.** 8N1 UART at 1 Mbaud, but only bytes with exactly four 1-bits
  are sent, so every UART frame is 50 % high. Data symbols never hold a level
  longer than 2 µs; the preamble's longest pulse is 5 µs. Any single-bit error
  breaks the balance, so it is caught before the CRC is even checked.
* **Continuous stream.** The UART owns the pin permanently. A feeder task keeps
  its FIFO topped up with IDLE symbols (`0x55`), or with a frame when one is
  queued, so the line never rests and never switches signal source. The task
  sleeps on the driver's FIFO semaphore and wakes roughly every 0.6 ms.
* **Alignment.** Each frame starts with 16 × `0xF0`, which has a single falling
  edge per byte. That forces the receiving UART onto the correct byte boundary
  after power-up or any disturbance.
* **Frame.** `[0xF0 x16] [SOF 0x2B] [LEN SEQ CMD PAYLOAD.. CRC16]`, each byte
  sent as two nibble symbols. Max payload 16 bytes.
* **One-way reliability.** Send each command several times with the same
  `seq`; the receiver delivers the first good copy and drops the rest.
* **Receiver.** RX FIFO threshold lowered to 64 bytes so the interrupt has
  ~640 µs of slack at 1 Mbaud (the driver default leaves 80 µs, and an
  overflow resets the FIFO). UART framing errors and overflows are counted.

## Things to verify on hardware

* Scope the TX pin: a continuous 500 kHz square wave at idle; 5 µs pulses then
  data during a message; no gaps, glitches or long constant levels anywhere.
* Healthy receiver stats: `uart frame errors` and `uart overflows` stay at 0
  after start-up (a few at power-up are normal); `duplicates` ≈ (repeats − 1)
  × `ok`; `crc` and `symbol` errors ≈ 0.
* Rising `uart frame errors` point at the signal (wiring, level shifting,
  pulse distortion). Rising `uart overflows` point at CPU load (raise the RX
  task priority or pin it to the core not used by Wi-Fi).
* If the TX line ever shows a gap, the feeder task was starved for > 0.6 ms:
  raise its priority or pin it to a less busy core.

## Status

Builds warning-free with ESP-IDF v6.0.3 and v6.1 for ESP32 and ESP32-S3
(both roles). Uses only public driver APIs. The encoder/parser is covered by
a host test (bit-error injection, resync, duplicate filtering), and the line
coding by a bit-level simulation of three UART-receiver resync behaviours.

v2.0 replaced the earlier LEDC + UART pin-switching transmitter: switching
between two unsynchronised sources produced runt pulses and could cut off the
end of a frame. The TX API changed accordingly (`toslink_tx_send()` now takes
a wait time; the LEDC fields are gone).
