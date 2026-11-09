#pragma once
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

/** @brief Persisted broker configuration; fixed-size text fields must be NUL-terminated.
 * @note Changing this storage layout requires consideration of the version field
 * and existing NVS records. Feature flags default to disabled.
 */
struct MqttSettings {
    uint32_t version = 1;
    bool enabled = false, discovery = false, commands = false;
    uint16_t port = 1883;
    char host[128]{}, username[64]{}, password[128]{}, prefix[96] = "tech_rs";
};

/**
 * @brief Checks configuration version, field termination, hostname and topic syntax.
 * @param s Configuration to validate without mutation.
 * @return Empty string if valid, otherwise an English validation error.
 * @note Does not resolve the host or verify credentials; disabled MQTT permits an empty host.
 */
std::string validate_mqtt(const MqttSettings& s);
