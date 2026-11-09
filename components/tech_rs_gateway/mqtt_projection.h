// SPDX-License-Identifier: MIT
#pragma once
#include "menu_snapshot.h"
#include "mqtt_settings.h"
#include <functional>

namespace tech_rs_gateway {
/**
 * @brief Decides whether an existing discovery type must be removed.
 * @param existing Previously published Home Assistant component type.
 * @param desired Currently selected component type.
 * @param enabled Whether MQTT Discovery is enabled.
 * @return True only when discovery is disabled or the component type differs.
 * @note Temporary invisibility or stale values must only change availability.
 */
bool obsolete_discovery_type(const std::string& existing, const std::string& desired, bool enabled);

using LabelResolver = std::function<std::string(int)>;

/**
 * @brief Owns MQTT topic/payload projections; publishing and Discovery lifecycle remain with the
 * adapter.
 */
struct MqttProjection {
    std::string node, base, component, value, attributes, discovery_topic, discovery;
    bool available = false;
};

/**
 * @brief Projects one record into state, attributes and Home Assistant Discovery JSON.
 * @param item Owned record/capability snapshot.
 * @param settings Topic prefix and command policy; the caller decides whether Discovery is enabled.
 * @param identity Stable gateway identity used in Discovery unique IDs.
 * @param label Synchronous description-code resolver; must be safe on the calling thread.
 * @return Owned projection, or nullopt for an unsupported record or invalid numeric metadata.
 * @note Pure with respect to the controller and broker. No publication or command is performed.
 */
std::optional<MqttProjection> project_mqtt_item(const ItemSnapshot& item,
                                                const MqttSettings& settings,
                                                const std::string& identity,
                                                const LabelResolver& label);

/**
 * @brief Routes a scalar MQTT command through shared service validation.
 * @param service Controller service that owns locking and pending writes.
 * @param topic Full received MQTT topic.
 * @param payload Received scalar integer text.
 * @param settings Prefix and commands-enabled policy.
 * @param retained Retained flag received from the broker; true suppresses replay processing.
 * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
 * @return No result for unrelated/malformed topics or retained replay; otherwise structured
 * acceptance/error.
 * @note A live retained publication may arrive with retain=false. Publishers must not retain
 * commands. Actions are not routed here.
 */
std::optional<OperationResult>
route_mqtt_command(ControllerService& service, const std::string& topic, const std::string& payload,
                   const MqttSettings& settings, bool retained, uint32_t now);
} // namespace tech_rs_gateway
