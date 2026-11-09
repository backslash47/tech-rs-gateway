# tech-rs-gateway

A local RS gateway for TECH heating controllers, providing **Home Assistant
integration** and a **local web interface** through an ESP32 and MAX3232.

## Compatibility

**The controller must use the newer ST-505 Internet-module protocol.**
Older register-based variants are not supported, even if the controller has an
RS socket or its manual lists ST-505 compatibility.

The [compatibility guide](docs/compatibility.md) lists the reference model and
describes how to identify the required protocol. Compatibility with an unlisted
model cannot be determined from the module name alone.

## Features

- **Home Assistant integration:** sensors and supported controls appear through
  MQTT Discovery, without manually defining each entity.
- **Live readings:** temperatures, equipment states and other values exposed by
  the controller. For a mixing circuit, these can include water temperature,
  valve opening and pump state.
- **Remote settings:** change supported setpoints, switches and operating modes
  from Home Assistant or the local web interface.
- **Local operation:** access the controller on the local network without an
  eModul account or cloud connection.
- **Browser access in English, Czech and Polish**, plus MQTT for other integrations.
- **Wi-Fi firmware updates and diagnostic logs** through ESPHome.

The available readings and settings depend on what the controller exposes.
Curves and schedules can be viewed; editing them is not implemented. Supported
action buttons are optional experimental features, disabled by default.

## Requirements

- A controller using the protocol described above.
- An ESP32 board, a **3.3 V-compatible MAX3232** converter, controller cable and power supply.
- An MQTT broker for Home Assistant or other MQTT integrations.
- ESPHome to build and upload the firmware from source.

The [hardware guide](docs/hardware.md) includes a parts list and wiring diagrams.
The reference ESP board is LaskaKit ESP32-C3-LPKit v4.0; GPIO assignments are configurable.

## Getting started

1. Check [controller compatibility](docs/compatibility.md).
2. Assemble the [hardware](docs/hardware.md) with the MAX3232 interface.
3. Set controller communication to **Main**, enable **Internet module**, and
   connect the gateway to **RS MODUŁY** (RS MODULY).
4. Follow [installation](docs/installation.md) to build, upload and provision Wi-Fi.
5. Open the gateway's local web page and configure [MQTT and Home Assistant](docs/mqtt.md).

Use a trusted local network: the web interface has no login and MQTT uses TCP
without TLS. A VPN provides remote access to that network.

## Documentation

- [Compatibility and available functions](docs/compatibility.md)
- [Hardware and wiring](docs/hardware.md)
- [Installation](docs/installation.md)
- [MQTT and Home Assistant](docs/mqtt.md)
- [Protocol reference](docs/protocol.md)
- [Development and architecture](docs/development.md)
- [Contributing](CONTRIBUTING.md)

## License

This application is licensed under the [MIT License](LICENSE).

This is an independent project, not an official TECH product or affiliated with TECH.
