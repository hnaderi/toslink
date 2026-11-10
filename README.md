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
for (int i = 0; i < 3; i++) toslink_tx_send(&msg);   /* repeats share one seq */

/* receiver */
static void on_msg(const tl_msg_t *m, void *ctx) { /* runs in the RX task */ }
toslink_rx_config_t rx = TOSLINK_RX_DEFAULT_CONFIG(GPIO_NUM_5, on_msg);
ESP_ERROR_CHECK(toslink_rx_init(&rx));
```

Defaults use UART1 and LEDC timer 0 / channel 0; change them in the config
struct if your application already uses those. The TX pin must be routable
through the GPIO matrix: avoid the UART's dedicated IO-MUX TX pin (init
returns `ESP_ERR_NOT_SUPPORTED` with a log message if you pick one).

## How it works

* **Line coding.** 8N1 UART at 1 Mbaud, but only bytes with exactly four 1-bits
  are sent, so every UART frame is 50 % high. Data symbols never hold a level
  longer than 2 µs; the preamble's longest pulse is 5 µs. Any single-bit error
  breaks the balance, so it is caught before the CRC is even checked.
* **Idle.** Between messages an LEDC channel drives a 500 kHz square wave
  (no CPU). The receiver sees it as a stream of `0x55` and ignores it.
* **Sending.** The whole transmission fits in the UART FIFO and is written in
  one call. The pin is switched from LEDC to UART (GPIO matrix) while the UART
  sends preamble, and back while it sends trailing filler.
* **Alignment.** The `0xF0` preamble has a single falling edge per byte, which
  forces the receiving UART onto the correct byte boundary after the idle wave.
* **Frame.** `[0xF0 x16] [SOF 0x2B] [LEN SEQ CMD PAYLOAD.. CRC16]`, each byte
  sent as two nibble symbols. Max payload 16 bytes.
* **One-way reliability.** Send each command several times with the same
  `seq`; the receiver delivers the first good copy and drops the rest.

## Things to verify on hardware

* Scope the TX pin: 500 kHz square wave at idle; 5 µs pulses then data during a
  message; no long constant levels at the switch-over points.
* Rising `crc errors` / `symbol errors` on the receiver usually mean wiring,
  supply noise, or a pin conflict, not the protocol.
* `toslink_tx_send()` returns `ESP_ERR_TIMEOUT` if the calling task was
  preempted for more than ~80 µs at the wrong moment. Call it from a task on
  the core not used by Wi-Fi, at fairly high priority, and resend on timeout.

## Status

Builds warning-free with ESP-IDF v6.0.3 and v6.1 for ESP32 and ESP32-S3
(both roles). Uses only public driver APIs, so it does not depend on ESP-IDF's
internal HAL tables (which changed between 6.0 and 6.1).
Encoder/parser logic is covered by a host test (bit-error injection,
resync, duplicate filtering). Not yet tested on real Toslink hardware.
