// SPDX-License-Identifier: MIT
#include "../components/tech_rs_gateway/mqtt_projection.h"
#include "support/controller_peer.h"
#include <iostream>
using namespace tech_rs_gateway;

void publish(const std::string& topic, const std::string& payload, bool retain) {
    std::cout << "{\"topic\":" << DynamicMenu::quote(topic)
              << ",\"payload\":" << DynamicMenu::quote(payload)
              << ",\"retain\":" << (retain ? "true" : "false") << "}\n";
}

int main() {
    ControllerService service;
    ControllerPeer peer(service);
    peer.initialize();
    peer.text("<MU|1.0.0|1|101,0,1,1,759,0,0,30,010,050,30,1035|102,0,20,1,312,0,0,319,1,-1,0>");
    MqttSettings settings;
    settings.commands = settings.discovery = true;
    std::strcpy(settings.prefix, "test_gateway");
    auto publish_state = [&] {
        const auto item = service.next_item("", peer.now);
        assert(item);
        const auto value = project_mqtt_item(*item, settings, "test_device", [](int) {
            return "Water target";
        });
        assert(value);
        publish(value->discovery_topic, value->discovery, true);
        publish(value->base + "/state", value->value, true);
        publish(value->base + "/availability", value->available ? "online" : "offline", true);
        publish("test_gateway/availability", "online", true);
        publish("test_gateway/tech/availability", "online", true);
    };
    publish_state();
    std::cout << "{\"done\":true}" << std::endl;
    std::string input;

    while (std::getline(std::cin, input)) {
        const auto fields = MenuText::split(input, '\t');
        assert(fields.size() == 3);
        const auto result =
            route_mqtt_command(service, fields[0], fields[1], settings, fields[2] == "1", peer.now);

        if (result) {
            publish("test_gateway/result", result->json(), false);

            if (result->accepted()) {
                peer.dispatch_write();
                publish("test_gateway/result", service.snapshot(peer.now).result.json(), false);
                peer.text("<MU|1.0.0|0|101,0,1,1,759,0,0," + fields[1] + ",10,50,30,1035>");
                publish("test_gateway/result", service.snapshot(peer.now).result.json(), false);
                publish_state();
            }
        }

        std::cout << "{\"done\":true}" << std::endl;
    }
}
