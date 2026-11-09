# TECH Internet-module protocol

This reference describes the **newer ST-505 Internet-module protocol** implemented
by the gateway: block-based framing, session initialization, dynamic menu transfers
and commands. Controller support
and setup are listed in [compatibility](compatibility.md).

## Serial transport and frame format

The serial connection uses **9600 baud, 8 data bits, no parity and one stop bit**.
The reference MAX3232 interface requires no software inversion.

```text
02 26 | address (2 bytes) | fields | 02 18 | CRC (2 bytes)
```

- Address, field tags and field lengths are big-endian.
- Each ordinary field contains a two-byte tag, a two-byte length and that many
  value bytes. Values are padded to a multiple of four bytes; length excludes padding.
- CRC covers all bytes before the `02 18` trailer.
- CRC-16/MCRF4XX uses initial value `FFFF`, reflected polynomial `8408` and no
  final XOR. The check value for `123456789` is `6F91`; CRC octets are big-endian.
- `FFF4` identifies the Internet-module protocol role.

The shared C++ codec is in `wire.h/.cpp`. `FrameStream` assembles incoming UART
chunks by structural length, so marker-like bytes inside values remain payload.
`InternetSession` manages the session above this layer. The
[offline decoder](development.md#offline-protocol-decoder) accepts complete frames.

### Bootstrap envelopes

The short forms `1788 0000` and `1788 0101` are fixed-size bootstrap envelopes,
handled separately from ordinary length-prefixed fields.

Controller request:

```text
02 26 FF F4 17 88 00 00 02 18 5D 9F
```

Module response:

```text
02 26 FF F4 17 88 01 01 02 18 55 CE
```

### Recovery form

The decoder also accepts a compatibility form in which a `0400` route header
declares four or six bytes but is immediately followed by the `1788` data tag.
This is marked as recovery and triggers re-establishment of communication; it
does not count as an ordinary route acknowledgment.

## Transfer roles and messages

Controller packets use `0400` for routing/acknowledgment and `1788` for data.
Module responses reverse those tags.

| Data prefix | Meaning |
|---|---|
| `0101 0000 type length` | Transfer announcement; type and length are 16-bit big-endian |
| `0301 <bytes>` | Data fragment |
| `0301` | Explicit transfer end |
| `0302` | Idle/keepalive |

Transfer selectors are `0050` for module metadata/read requests and `0049` for
controller text and text commands.

The ready route is `0201 FFFF`. The longer acknowledgment route is
`04 count_hi count_lo flag FFFF`, padded to field alignment. Count represents
accumulated data bytes; flag 1 means continuing and flag 3 means complete.

## Initialization and metadata

Following bootstrap, controller requests advance the initialization sequence:
`030100`, empty `0301`, an announced `0050` metadata transfer, metadata data,
and an explicit transfer end. An incoming `<INIT|...>` record transitions the
gateway to the initialized state.

Metadata records consist of a one-byte length, a two-byte **little-endian** record
ID, and the value. Their length includes the ID and value bytes. This byte order
differs from the outer frame fields.

| Record ID | Gateway content |
|---|---|
| `0001` | DHCP-related flag |
| `0002`–`0005` | IPv4 address, subnet mask, gateway and DNS address |
| `002A` | Emulated module model 505 |
| `0014`, `002B` | Identity fields populated from local MAC-derived data |
| `0015`, `0028`, `001E`, `000A` | Compatibility constants; exact field semantics remain unspecified |

The gateway supplies its own network metadata and identity values.

## Reading and fragmented transfers

The read request is a four-byte `0050` transfer containing `03 1E 00 01`, followed
by an explicit end. The gateway schedules it every 10 seconds when the connection
is free of another read, write or incoming transfer.

Incoming text transfers use selector `0049`. The gateway accumulates fragments,
acknowledges their byte count and parses the `<...>` text. The empty `0301`
terminator releases receive ownership. New outgoing writes wait for that boundary.

### Delta updates and freshness

The controller can omit unchanged values from subsequent updates. A ready route
followed by acknowledgment of the four-byte read request renews poll freshness.
Idle frames alone do not renew it.

Records are associated with a session generation. After reconnection or a new
controller bootstrap, prior-generation records remain stale until replaced by
fresh records. A poll acknowledgment does not confirm an outstanding setting write.

## Menu records

Text groups are **MU** (user), **MI** (installation), **MS** (service) and
**SU** (overview). Sections are separated by `|`; item fields by commas.
Ordinary menu records begin with item ID, parent ID, type, visibility and
description code. SU layouts vary by type.

Item IDs, hierarchy, bounds and options come from the connected controller.
Description codes resolve through the [shared dictionaries](development.md#shared-compressed-dictionaries).

| Type | Gateway interpretation |
|---|---|
| Menu 0 | Hierarchical group |
| Menu 1–3 | Integer/raw numeric value and bounds |
| Menu 10 | Boolean value |
| Menu 11 | Enumeration of label codes and numeric option values |
| Menu 20 | Optional action dialog; confirmation values by subtype: 0→2, 1→1, 2→3 |
| Menu 12 | Weekly schedule, read-only |
| Menu 30 | Multi-point curve, read-only |
| SU 1 | Temperature, with missing-value sentinels |
| SU 23 | Valve overview: temperature, opening and pump state |
| SU 40 / SU 50 | Controller state / module version |

Unsupported records remain available as raw data.

## Writing values

```text
<X|0.0.1|ID,value>
```

`ID` is a controller menu item ID and `value` is an integer in that item's
protocol units. For example, `<X|0.0.1|101,31>` is a 16-byte command for an
illustrative item 101. IDs are resolved from the current menu rather than fixed
for particular settings.

1. Validate item type, visibility, bounds/options and the expected previous value.
2. Announce a `0049` transfer with the command's ASCII byte length.
3. Wait for the ready route, then send `0301` followed by the command once.
4. Wait for acknowledgment of that byte count and close with empty `0301`.
5. Confirm a scalar change only after a fresh matching controller record arrives.

Transport acknowledgment means delivery. Action delivery is reported separately
from execution because it has no equivalent scalar readback. Reconnection cancels
partial transactions without automatically replaying a potentially executed command.
Client-visible results are documented in [MQTT](mqtt.md#structured-command-results).

## Gateway limits

These are implementation limits, defined in `protocol_constants.h`:

| Limit | Value |
|---|---:|
| Frame buffer | 4096 bytes |
| Assembled text | 16384 bytes |
| Stalled receive timeout | 15 seconds |
| Write transfer timeout | 30 seconds |
| Scalar readback deadline | 120 seconds |

Memory pressure is tracked as an incomplete menu rather than interpreted as
absence of a controller setting.

## Technical sources and examples

- [Node-RED flow, Elektroda post #338](https://www.elektroda.pl/rtvforum/topic2689981-330.html): initialization, metadata and read requests.
- [i-3 Plus OT implementation, post #380](https://www.elektroda.pl/rtvforum/topic2689981-360.html#21954827): transfer handling and receive ownership.
- [dzien.net captured frames](https://dzien.net/tech/index.php?baza=ramki): text-command examples.
- [tech-controllers integration](https://github.com/mariusz-ostoja-swierczynski/tech-controllers): menu and action-dialog semantics.
- [techReader](https://github.com/ckszabi/techReader) and [Tech_RS-mqtt-gateway](https://github.com/agirilovich/Tech_RS-mqtt-gateway): serial receive research.
- [st480-gateway](https://github.com/mkubi/st480-gateway): an older bidirectional protocol variant.

The [protocol fixtures](../tests/fixtures/README.md) provide minimal envelopes
and a write-transfer example with direction and CRC information.
