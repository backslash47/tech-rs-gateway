// SPDX-License-Identifier: MIT
#include "../components/tech_rs_gateway/mqtt_projection.h"
#include "support/controller_peer.h"
#include <iostream>
using namespace tech_rs_gateway;

int main() {
    ControllerService service;
    ControllerPeer peer(service);
    peer.initialize();
    peer.text("<MU|1.0.0|1|101,0,1,1,759,0,0,30,010,050,30,1035|102,0,20,1,312,0,0,319,1,-1,0>");
    MqttSettings settings;
    settings.commands = settings.discovery = true;
    const auto label = [](int id) {
        return std::to_string(id);
    };
    auto item = service.next_item("", peer.now);
    assert(item && item->key == "MU:101");
    auto projected = project_mqtt_item(*item, settings, "test_device", label);
    assert(projected && projected->component == "number" && projected->value == "30");

    for (const std::string kind : {"sensor", "number", "switch", "select"}) {
        assert(obsolete_discovery_type(kind, projected->component, true) == (kind != "number"));
        assert(obsolete_discovery_type(kind, projected->component, false));
    }

    auto stale = *item;
    stale.fresh = false;
    stale.visible = false;
    const auto hidden = project_mqtt_item(stale, settings, "test_device", label);
    assert(hidden && !hidden->available);
    assert(!obsolete_discovery_type("number", hidden->component, true));
    assert(projected->discovery.find("\"min\":10,\"max\":50") != std::string::npos);
    assert(projected->discovery.find("\"retain\":false") != std::string::npos);
    assert(projected->discovery.find("\"sw_version\":") != std::string::npos);
    const auto topic = std::string(settings.prefix) + "/menu/MU/101/set";
    assert(!route_mqtt_command(service, topic, "31", settings, true, peer.now));
    assert(!service.snapshot(peer.now).busy());
    assert(route_mqtt_command(service, topic, "99", settings, false, peer.now)->code ==
           ResultCode::OutOfRange);
    assert(route_mqtt_command(service, std::string(settings.prefix) + "/menu/MU/102/set", "1",
                              settings, false, peer.now)
               ->code == ResultCode::NotWritable);
    assert(route_mqtt_command(service, topic, "31", settings, false, peer.now)->accepted());
    assert(peer.dispatch_write() == "<X|0.0.1|101,31>");
    assert(service.snapshot(peer.now).write_state == WriteState::AwaitingReadback);
    peer.text("<MU|1.0.0|0|101,0,1,1,759,0,0,31,10,50,30,1035>");
    assert(service.snapshot(peer.now).result.code == ResultCode::Confirmed);
    item = service.next_item("", peer.now);
    assert(project_mqtt_item(*item, settings, "test_device", label)->value == "31");
    settings.commands = false;
    projected = project_mqtt_item(*item, settings, "test_device", label);
    assert(projected && projected->component == "sensor");
    assert(projected->discovery.find("\"state_class\"") == std::string::npos);
    assert(route_mqtt_command(service, topic, "32", settings, false, peer.now)->code ==
           ResultCode::CommandsDisabled);
    peer.text("<SU|1.0.0|1|501,0,1,1,0,759,not-a-temperature>");
    item = service.next_item("SU:0", peer.now);
    projected = project_mqtt_item(*item, settings, "test_device", label);
    assert(projected && !projected->available && projected->value.empty());
    peer.text("<SU|1.0.0|0|501,0,1,1,0,759,215>");
    item = service.next_item("SU:0", peer.now);
    projected = project_mqtt_item(*item, settings, "test_device", label);
    assert(projected && projected->available && projected->component == "sensor");
    assert(projected->discovery.find("\"state_class\":\"measurement\"") != std::string::npos);
    peer.text(
        "<SU|1.0.0|1|6016,0,23,1,0,101,275,0,0,50,1,0,0,32|7024,0,23,1,0,102,341,0,0,85,0,0,0,40>");
    settings.commands = true;

    for (bool second : {false, true}) {
        item = service.next_item(second ? "SU:6016" : "SU:6015", peer.now);
        const std::string id = second ? "7024" : "6016";
        assert(item && item->key == "SU:" + id);
        const auto field = [&](MqttChannel channel) {
            return project_mqtt_item(*item, settings, "test_device", label, channel).value();
        };
        const auto opening = field(MqttChannel::Primary);
        assert(opening.node == "SU_" + id && opening.value == (second ? "85" : "50"));
        assert(opening.attributes.find(item->record.raw) != std::string::npos);
        const auto temp = field(MqttChannel::Temperature),
                   target = field(MqttChannel::TargetTemperature), pump = field(MqttChannel::Pump);
        assert(temp.node == opening.node + "_temperature" &&
               std::stof(temp.value) == (second ? 34.1f : 27.5f));
        assert(target.node == opening.node + "_target_temperature" &&
               target.value == (second ? "40" : "32"));
        assert(temp.discovery.find("\"state_class\":\"measurement\"") != std::string::npos &&
               target.discovery.find("state_class") == std::string::npos);
        assert(pump.discovery_topic ==
                   "homeassistant/binary_sensor/test_device/SU_" + id + "_pump/config" &&
               pump.value == (second ? "OFF" : "ON"));

        for (const auto* p : {&temp, &target, &pump}) {
            assert(p->available && p->base.rfind(opening.base + "/", 0) == 0 &&
                   p->discovery.find("command_topic") == std::string::npos);
        }

        peer.text("<SU|1.0.0|0|" + id + ",0,23,1,0,101,-2000,0,0,50,9,0,0,32>");
        item = service.next_item(second ? "SU:6016" : "SU:6015", peer.now);
        assert(!field(MqttChannel::Temperature).available && !field(MqttChannel::Pump).available &&
               field(MqttChannel::TargetTemperature).available);
    }

    std::cout << "PASS: MQTT Discovery projection, retained replay, validation and headless "
                 "write/readback\n";
}
