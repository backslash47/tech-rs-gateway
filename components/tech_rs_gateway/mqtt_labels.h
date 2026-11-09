// SPDX-License-Identifier: MIT
#pragma once

#include <string>

/**
 * @brief Resolves a controller description using the build-selected MQTT language.
 * @param code Numeric description code, not a menu item ID.
 * @return Owned decoded label, or a TECH-code fallback if lookup/decoding fails.
 * @note Uses a mutable shared block cache; call from the MQTT service/main loop,
 * not concurrently from HTTP handlers. Browser language selection has no effect.
 */
std::string mqtt_menu_label(int code);
