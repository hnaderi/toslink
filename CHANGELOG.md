# Changelog

## 0.1.0

First public release.

* Handle-based API: `toslink_tx_new/del/send`, `toslink_rx_new/del/get_stats`.
* The transmitter assigns sequence numbers, and the receiver reports gaps as
  `frames_lost`. Caller-managed repeats and receive-side duplicate filtering
  were removed.
* The wire format is unchanged from the prototype.
