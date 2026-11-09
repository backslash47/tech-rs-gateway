# Development

Use the pinned Python dependencies in `requirements-dev.txt`. Web tooling
requires Node.js 22 or newer and `npm ci`. Coding conventions and formatter
commands are in [CONTRIBUTING.md](../CONTRIBUTING.md).

## Source layout

| Directory | Contents |
|---|---|
| `components/tech_rs_gateway/` | Transport, menu model, web handler, MQTT and ESPHome adapter |
| `config/` | Reference board and local-configuration examples |
| `web/` | HTML, CSS, JavaScript and interface translations |
| `vendor/emodul/` | Controller-label dictionaries |
| `tools/` | Asset generator, offline decoder and test runners |
| `tests/` | Host tests, simulated web tests and protocol fixtures |

Headers declare interfaces and types; `.cpp` files contain implementations.
Only small inline wire helpers remain in headers. Each implementation includes
its own header and its direct dependencies.

### Controller service and adapters

`ControllerService` owns the menu model and `InternetSession`. It coordinates
write dispatch, transport acknowledgment, readback, reconnect generations and
timeouts independently of HTTP and MQTT. Its request and snapshot methods lock
internally and return owned data. Adapter code never holds the model mutex while
performing network I/O.

`InternetSession` is single-threaded and uses the shared `wire` codec through
`FrameStream`. The stream layer finds frame boundaries from TLV lengths, including
when header/trailer bytes appear inside a fragmented payload. Initialization,
polling and write transfer stages have named enum states.

`DynamicMenuWeb` only handles HTTP concerns. `MenuApi` validates menu requests
and serializes snapshots without HTTP-server dependencies. `MenuMqtt` owns the
broker client; `mqtt_projection` creates Discovery/state payloads and routes
commands through the same controller service. The pure `MenuApi` and
`mqtt_projection` modules are tested without ESP-IDF.

Protocol tags, field positions, limits and timing constants are named in
`protocol_constants.h`. Application writes use `WriteState`; structured outcomes
use `ResultCode` and `OperationResult`. An ACK is not a confirmed scalar write.

ESPHome copies the component directory from `includes` and compiles its `.cpp`
files. After adding or removing source files, clear an existing build directory
with `esphome clean config/esp32-c3-lpkit.yaml` so ESP-IDF refreshes its source list.

## Automated checks

Generate assets before compiling host tests or the reference firmware:

```sh
python tools/build_assets.py
python tools/check_docs.py
python tools/test_host.py
python tools/test_labels.py
python -m unittest discover -s tests -p 'test_*.py' -v
python -m playwright install chromium
RUN_WEB_TESTS=1 python -m unittest discover -s tests -p 'test_*.py' -v
esphome compile config/esp32-c3-lpkit.yaml
```

The host runners link the same implementation files as firmware and enable
AddressSanitizer/UndefinedBehaviorSanitizer. The label test additionally needs
the host zlib development library. On Debian/Ubuntu, install `zlib1g-dev`.

UI tests intercept requests using synthetic data. Dictionary delivery tests use
a local HTTP server and real browser decompression. Neither contacts a heating
controller or a production broker. `CHROMIUM_EXECUTABLE` can select a locally
installed Chrome/Chromium executable instead of Playwright's bundled browser.

GitHub Actions runs style, host, web and reference-build checks. It does not
upload firmware. Controller requirements and functional limitations are listed in
[compatibility](compatibility.md).

The documentation job also checks API comments and runs positive/negative parser
fixtures. Documentation conventions and scope are in
[Contributing](../CONTRIBUTING.md#api-documentation).

### MQTT integration test

```sh
pip install amqtt==0.11.3 paho-mqtt==2.1.0
RUN_MQTT_TESTS=1 python -m unittest discover -s tests -p 'test_mqtt_broker.py' -v
```

This starts an isolated broker on loopback with an ephemeral port. A C++ harness
uses the production projection/router/service with a synthetic TECH peer. It
checks retained Discovery/state, ignored retained command replay, rejection of
invalid values/actions, and queued→sent→confirmed write results. It does not
exercise the ESP-IDF MQTT driver or a physical controller.

Session tests perform bootstrap, metadata exchange, INIT, polling and writes
through public APIs rather than assigning internal flags. HTTP tests cover
authorization/errors, response lifetime without locks and competing request threads.

## Offline protocol decoder

```sh
python tools/decode_frames.py tests/fixtures/internet-envelope.hex
python tools/decode_frames.py tests/fixtures/write-transport.hex
```

The decoder accepts complete hexadecimal frames, one per line, and produces
JSON lines. Invalid input produces an error record and nonzero exit status.
It validates framing and CRC; it does not transmit or anonymize data. Fixture
origin and direction are documented in [fixture notes](../tests/fixtures/README.md).

## Web assets and translations

### Application identity

`VERSION` defines the application version. Asset generation creates `build_info.h`
and an embedded `BUILD_INFO` object from that version and a deterministic source
fingerprint. The web's information dialog and MQTT Discovery device `sw_version`
show the same version/build pair. Include it when reporting a problem.

The fingerprint covers handwritten firmware, web sources, dictionary inputs,
the asset generator, pinned Python dependencies, reference configuration and MQTT
language. Generated files and local secrets are excluded. It identifies the source
bundle, not the final binary or arbitrary local compiler/configuration overrides.
Regenerate assets before compiling a changed source tree. Dependencies are managed
with `requirements-dev.txt` and `package-lock.json`; this is not a uv-locked project.

### Packaging

`tools/build_assets.py` inlines `index.html`, `style.css`, JavaScript and
`web/locales/*.json` into the served page. Interface translations are separate
from the controller descriptions in `vendor/emodul/*.json`.

Web responsibilities are separated into `state.js`, `ui.js`, `menu-api.js`,
`menu-renderer.js`, `menu-editors.js`, `menu-dialogs.js` and `mqtt-settings.js`.
`app.js` contains startup/event wiring. They are loaded in the order declared in
`index.html`, without a bundler dependency. Shared UI state lives in `appState`;
the `results` locale maps translate structured result codes.

The generator emits two header/source pairs:

- `dynamic_menu_assets.h/.cpp`: compressed web page and dictionary bytes.
- `label_dictionary_data.h/.cpp`: block indexes and the selected MQTT dictionary view.
- `build_info.h`: application version and source fingerprint.

Headers declare external arrays with fixed sizes; `.cpp` files define them once.
These are ignored build outputs and must not be edited by hand.

## Shared compressed dictionaries

Every numeric description code is sorted and grouped into complete JSON-entry
fragments, each at most **16 KiB** when decoded. Each fragment is an independent
gzip member. Concatenating their decoded contents produces the entire dictionary.
Web and MQTT reference the same stored bytes.

For MQTT, `CompressedLabels` binary-searches the block index and caches one
decoded block. It scans JSON tokens and uses ArduinoJson to decode the selected
label. The ESP adapter uses ROM miniz and checks the gzip header, consumed input,
decoded length, ISIZE and CRC32. Decompressor state is allocated on the heap,
not the task stack. A missing code or decode/allocation failure produces the
`TECH <code>` fallback; failed inflation is not cached as successful data.

`GET /labels.gz?lang=en` (also `cs` and `pl`) serves the shared bytes as an
octet stream. `X-Tech-Dictionary-Blocks` lists compressed member lengths.
`web/dictionary.js` validates boundaries and decodes members individually using
`DecompressionStream('gzip')`, then parses the combined JSON. This avoids reliance
on browsers' inconsistent handling of concatenated members in HTTP gzip decoding.
A browser supporting `DecompressionStream` is required.

The full dictionary is cached only in the browser. ESP persistent lookup storage
is one 16 KiB block, plus temporary miniz state and the selected JSON/string value.
The HTTP handler sends stored bytes without accessing the mutable MQTT cache.

## Reference resource budget

Measured with ESPHome 2026.6.3 on the reference configuration, 2026-10-29:

| Resource | Size |
|---|---:|
| Physical flash | 4 MiB |
| Each OTA application slot | 1,835,008 bytes |
| Program usage | 1,744,854 bytes (95.1%) |
| Remaining program allowance | 90,154 bytes |
| Static RAM | 45,908 bytes |

Static RAM does not include runtime heap allocations. These figures are a
reference measurement, not a permanent size guarantee; check build output after
changing dependencies, features or assets. OTA capacity is limited by one
application slot, not the total physical flash.

| Shared dictionary | Compressed bytes | Blocks |
|---|---:|---:|
| Czech | 187,887 | 35 |
| Polish | 259,884 | 49 |
| English | 229,930 | 45 |

The block format bounds lookup memory without storing a second uncompressed
MQTT dictionary. Tests compare every label with its source JSON and verify full
dictionary delivery in Chromium.
