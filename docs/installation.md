# Installation and configuration

**Check [controller compatibility](compatibility.md) first:** this gateway requires
the newer ST-505 Internet-module protocol. Follow the [wiring guide](hardware.md)
before connecting hardware.
The reference configuration uses ESPHome **2026.6.3**.

## Required controller configuration

Configure the controller for communication with an Internet module:

| Setting / connection | Required value |
|---|---|
| Communication mode | **Main** |
| Internet module | **Enabled** in the controller settings |
| Gateway cable | **RS MODUŁY** (RS MODULY) socket |

Connect the cable through the MAX3232 as shown in the [wiring diagrams](hardware.md),
with power disconnected while wiring. If the gateway web opens but no controller
menu appears, check these three conditions before troubleshooting Wi-Fi or MQTT.

## Build from source

Use Python 3.12 and the pinned dependencies:

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements-dev.txt
python tools/build_assets.py
esphome compile config/esp32-c3-lpkit.yaml
```

Asset generation is offline: it reads `web/` and the bundled dictionaries and
creates generated `.h` / `.cpp` files. Firmware output is under
`config/.esphome/`. Both generated assets and build output are ignored by Git.

To choose the MQTT label language, generate assets with `--mqtt-language en`,
`cs` or `pl`. English is the default. This choice is independent of the web
language selected in the browser.

## Board configuration

`config/esp32-c3-lpkit.yaml` targets LaskaKit ESP32-C3-LPKit v4.0. Its substitutions
configure UART RX/TX and peripheral-power enable. For a different board, also
review the board identifier and USB logger settings. GPIO4 supply switching is
specific to the reference board; remove that switch on boards without it.

Controller mode and socket requirements are listed above under
[Required controller configuration](#required-controller-configuration).

## Upload

For the initial upload, connect the ESP to the computer with a data-capable USB
cable. Replace the example port with the device's actual serial port:

```sh
esphome upload config/esp32-c3-lpkit.yaml --device /dev/ttyACM0
```

Once the device has Wi-Fi access, OTA uploads and logs use its LAN IP address:

```sh
esphome upload config/esp32-c3-lpkit.yaml --device <ESP_IP>
esphome logs config/esp32-c3-lpkit.yaml --device <ESP_IP>
```

Rebuild after source/configuration changes before uploading. OTA requires a
reachable, running device; keep USB access available for recovery.

## Wi-Fi

Without stored credentials, connect to the **TECH RS Setup** access point and
use its captive portal to configure Wi-Fi. Alternatively, provide credentials
at build time:

1. Copy `config/wifi-secrets.example.yaml` to `config/wifi-local.yaml`.
2. Copy `config/secrets.example.yaml` to `config/secrets.yaml` and edit the values.
3. Compile and upload `config/wifi-local.yaml` instead of the reference YAML.

Both local files are ignored. Do not put credentials in the committed examples.

## Web and MQTT

Open `http://<ESP_IP>/` to browse the dynamic controller menu. To connect a broker,
open **Settings · MQTT**. MQTT is disabled until configured. See the
[MQTT guide](mqtt.md) for Discovery, topics, commands and availability.

The web has no login and MQTT uses TCP without TLS. Use a trusted network and
a VPN for remote access rather than exposing these services directly online.

## Experimental actions

The `experimental_actions` substitution defaults to `'false'`. Set it to `'true'`
and rebuild to enable supported action dialogs. Each action still requires web
confirmation; transport acknowledgment does not establish successful execution.
See [compatibility](compatibility.md) for available functions and limitations.
