// SPDX-License-Identifier: MIT
#include "mqtt_settings.h"

std::string validate_mqtt(const MqttSettings& s) {
    if (s.version != 1 || !s.port) {
        return "Invalid port";
    }

    if (!memchr(s.host, 0, sizeof(s.host)) || !memchr(s.username, 0, sizeof(s.username)) ||
        !memchr(s.password, 0, sizeof(s.password)) || !memchr(s.prefix, 0, sizeof(s.prefix))) {
        return "Field too long";
    }

    if (s.enabled && !s.host[0]) {
        return "Broker is required";
    }

    for (const unsigned char* p = (const unsigned char*)s.host; *p; ++p) {
        if (!std::isalnum(*p) && *p != '.' && *p != '-' && *p != ':' && *p != '[' && *p != ']') {
            return "Invalid broker hostname";
        }
    }

    const std::string prefix = s.prefix;

    if (prefix.empty() || prefix.front() == '/' || prefix.back() == '/' ||
        prefix.find("//") != std::string::npos) {
        return "Invalid topic prefix";
    }

    for (unsigned char c : prefix) {
        if (!std::isalnum(c) && c != '_' && c != '-' && c != '/') {
            return "Invalid topic prefix";
        }
    }

    return "";
}
