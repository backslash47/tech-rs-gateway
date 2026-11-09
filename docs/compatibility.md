# Compatibility and limitations

## Protocol compatibility

The controller must use the **newer ST-505 Internet-module protocol**. An RS socket
or a manual listing ST-505 support is a starting point, but not sufficient proof:
older register-based communication variants are not supported by this gateway.

## Reference controller

The project was developed using **TECH i-1**
([manufacturer manual](https://tech-controllers.com/!uploads/manuals_eu/eu-i-1_cz.pdf)).
Additional controller models must use the protocol described above; a complete
model compatibility list is not yet available.

### Identifying an unlisted controller

Check the controller manual for Internet-module support and its connection port.
To establish protocol compatibility, look for a communication trace or implementation
for the same model and firmware using block transfers `0400` / `1788` and text
groups `MU`, `MI`, `MS` or `SU`. These are the technical identifiers of the supported
variant; [protocol.md](protocol.md) explains their structure.

If that information is unavailable, the model's compatibility remains unknown.

## Required setup

- Communication mode: **Main**.
- **Internet module enabled** in the controller settings.
- Gateway connected to **RS MODUŁY** (RS MODULY).

See
[installation](installation.md#required-controller-configuration) and
[hardware](hardware.md) for configuration and wiring.

## Available functions

The exact set depends on the connected controller. A mixing controller can expose
the following readings and settings:

| Function | Availability |
|---|---|
| Temperatures and equipment states | Live readings in the web interface and supported MQTT entities |
| Requested water temperature | Adjustable through the web interface and MQTT |
| Valve opening and reported pump state | Readings where provided by the controller |
| Pump operating mode | Adjustable where provided by the controller |
| Other setpoints, switches and selections | Reading and writing for supported setting types |
| Calibration and other supported actions | Optional experimental actions, disabled by default |
| Curves and schedules | Display only |

## Limitations

- Unsupported menu types retain raw data but may lack a specialized display/editor.
- Writes use integer protocol values and the bounds/options supplied by the controller.
- Transport acknowledgment confirms delivery, not execution. Scalar writes require
  fresh readback; action delivery is not a completion signal.
- Pump operating mode and actual pump state are distinct. Stopping the pump does
  not guarantee closure of the mixer's hot inlet.
- The web has no authentication and MQTT does not use TLS. Use a trusted LAN.
