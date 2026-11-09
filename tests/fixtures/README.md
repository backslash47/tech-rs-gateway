# Protocol fixtures

## Envelope vectors

`internet-envelope.hex` contains minimal vectors for the empty Internet bootstrap,
module reset response and controller-ready response. CRC bytes are checked by
the C++ and Python decoders. Corruption, padding and embedded-marker cases are
generated synthetically by the tests.

## Captured write exchange

`write-transport.hex` contains five consecutive frames selected from a project
capture dated 2026-10-26. Packet bytes are unchanged; timestamps, log wrappers
and the rest of the session are omitted. Comments identify direction relative
to the gateway.

The exchange announces a 17-byte text transfer, sends `<X|0.0.1|8307,34>`, receives
a byte-count acknowledgment and closes the transfer. Item ID and value belong
to that record; implementations obtain them from the current controller menu.
The excerpt covers transport delivery, not subsequent setting readback.

The fixtures contain no network addresses, MACs or credentials. `FFF4` denotes
the protocol role. The decoder runs offline:

```sh
python tools/decode_frames.py tests/fixtures/write-transport.hex
```
