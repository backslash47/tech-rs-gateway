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
    assert(project_mqtt_item(*item, settings, "test_device", label)->component == "sensor");
    assert(route_mqtt_command(service, topic, "32", settings, false, peer.now)->code ==
           ResultCode::CommandsDisabled);
    peer.text("<SU|1.0.0|1|501,0,1,1,0,759,not-a-temperature>");
    item = service.next_item("SU:0", peer.now);
    projected = project_mqtt_item(*item, settings, "test_device", label);
    assert(projected && !projected->available && projected->value.empty());
    std::cout << "PASS: MQTT Discovery projection, retained replay, validation and headless "
                 "write/readback\n";
}
