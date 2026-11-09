// SPDX-License-Identifier: MIT
#include "../components/tech_rs_gateway/mqtt_settings.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    MqttSettings settings;
    assert(validate_mqtt(settings).empty());
    assert(!settings.enabled && !settings.discovery && !settings.commands);

    settings.enabled = true;
    assert(!validate_mqtt(settings).empty());
    std::strcpy(settings.host, "mqtt.example.local");
    assert(validate_mqtt(settings).empty());

    for (const auto* prefix : {"", "/gateway", "gateway/", "a//b", "a/+", "a/#", "a b"}) {
        std::strcpy(settings.prefix, prefix);
        assert(!validate_mqtt(settings).empty());
    }

    std::strcpy(settings.prefix, "heating/tech_gateway-1");
    assert(validate_mqtt(settings).empty());
    settings.port = 0;
    assert(!validate_mqtt(settings).empty());
    settings.port = 1883;
    std::strcpy(settings.host, "mqtt://example.local");
    assert(!validate_mqtt(settings).empty());
    std::strcpy(settings.host, "mqtt.example.local");

    std::memset(settings.password, 'x', sizeof(settings.password));
    assert(!validate_mqtt(settings).empty());
    settings.password[sizeof(settings.password) - 1] = '\0';
    assert(validate_mqtt(settings).empty());

    std::cout
        << "PASS: disabled defaults, broker required, topic validation, bounds and termination\n";
}
