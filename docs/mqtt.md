# MQTT and Home Assistant

MQTT is configured through the gateway web interface and stored on the ESP.
Broker, username, password, port and topic prefix are not compiled into the
reference YAML. The bridge starts disabled until configured.

Discovery device information includes the gateway application version and build
identifier in `sw_version`, matching the local web information dialog.

## Connecting

1. Provision Wi-Fi and open the gateway's local HTTP address.
2. Open **Settings · MQTT** and enter the broker host, port and credentials.
3. Set a unique topic prefix for this gateway.
4. Enable MQTT and, optionally, Discovery and commands.
5. Save and check the connection status.

The broker must be reachable from the ESP. Default port is 1883 (MQTT/TCP, without
TLS). An empty password field preserves a stored password; use the explicit
delete-password option to clear it. The web does not reveal the stored password.

## Topics

In the following table, `<prefix>` is your configured prefix, `<group>` is MU,
MI, MS or SU, and `<id>` is an item supplied by the connected controller.

| Topic | Content | Retained |
|---|---|---|
| `<prefix>/availability` | Gateway MQTT connection, `online` / `offline` (LWT) | Yes |
| `<prefix>/tech/availability` | Current controller-session availability | Yes |
| `<prefix>/menu/<group>/<id>/state` | Item value | Yes |
| `<prefix>/menu/<group>/<id>/attributes` | JSON: menu, ID, type, visibility, raw record | Yes |
| `<prefix>/menu/<group>/<id>/availability` | Availability of this item | Yes |
| `<prefix>/menu/<group>/<id>/set` | Incoming scalar command | **No** |
| `<prefix>/result` | Immediate command acceptance/error or subsequent write status | No |

Published data uses QoS 1. HA Discovery configuration is retained under
`homeassistant/<component>/<device identity>/<group>_<id>/config`.

### Structured command results

Results are machine-readable and independent of the UI language:

```json
{"code":"write_confirmed","item":"MU:101","value":31}
```

Successful scalar writes progress through `write_queued`, `write_sent` and
`write_confirmed`. Only the last means fresh controller readback matched the
request. Errors include `busy`, `offline`, `missing_item`, `stale_metadata`,
`not_writable`, `value_conflict`, `invalid_integer`, `out_of_range`,
`invalid_boolean`, `unknown_option` and `commands_disabled`.
`write_cancelled`, `write_reconnected` and `write_timeout` are terminal outcomes
without successful confirmation. The `value` field is null when no value applies.

The web API uses the same result object for write responses and exposes the
latest operation under `result` in menu snapshots. Clients should use `code`,
not parse human-readable text.

## Reading and changing values

Numbers and switches use raw protocol values. For example, if the controller
exposes a numeric item `MU:101`, a payload of `31` on
`<prefix>/menu/MU/101/set` requests the value 31. This is an illustrative ID;
use the item ID, units and bounds supplied by the connected controller's menu.

Select topics use the controller's numeric option value. Discovery provides
templates that translate between these values and the human-readable options in
HA. The numeric suffix in an option label keeps similarly named choices distinct.

Before accepting a command, the bridge checks that MQTT commands are enabled,
the controller is online, item metadata is fresh, the item supports writing,
and the value satisfies its type and limits. A command accepted for transmission
is **not yet confirmed as applied**. Subsequent fresh controller data confirms a
scalar write. A timeout means unconfirmed, not proof the controller did nothing.
Writes are not automatically replayed after reconnection.

Do not retain command messages. Retained messages delivered as retained by the
broker on subscription are ignored. This is not a universal retained-command
firewall: brokers can forward a newly published retained message to existing
subscribers without the retained flag. Configure publishers to use `retain:false`.

Action commands (including calibration) are rejected through MQTT. Experimental
actions can only be invoked through an enabled web confirmation dialog.

## Discovery defaults

- Supported scalar items are discovered automatically.
- MU and SU entities are enabled by default; MI and MS entities are disabled by
  default in HA to avoid filling dashboards with installation/service controls.
- Gateway/API logging remains available, but this public configuration does not
  also publish fixed native ESPHome HA entities.
- Entities depend on the menu actually received. Some items are conditional or
  hidden by the controller. Discovery is not a promise that every menu type has
  an editable HA representation.

Unchanged values can remain valid while the TECH stream sends only deltas. A
confirmed poll renews validity within the same session. A reconnect requires
fresh records and cannot revive a previous session's snapshot.

Restarts and temporary loss or hiding of menu items preserve existing Discovery
records; availability becomes offline instead. This keeps entity IDs and user
customizations in Home Assistant. Obsolete component types are removed when a
type changes, and known records are removed when Discovery is disabled.

## Label language

MQTT uses the complete selected TECH dictionary, shared with the web as compressed
blocks. Choose the MQTT language when generating assets:

```sh
python tools/build_assets.py --mqtt-language en
```

`cs` and `pl` are also supported. Changing the browser UI language does not rename
HA entities. Names customized in HA may also override subsequently published
Discovery names. Item identities and topics do not depend on translated names.
