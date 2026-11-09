// SPDX-License-Identifier: MIT
#include "mqtt_projection.h"
#include "build_info.h"
#include "controller_service.h"

namespace tech_rs_gateway {
bool obsolete_discovery_type(const std::string& existing, const std::string& desired,
                             bool enabled) {
    return !enabled || existing != desired;
}

std::optional<MqttProjection> project_mqtt_item(const ItemSnapshot& snapshot,
                                                const MqttSettings& settings,
                                                const std::string& identity,
                                                const LabelResolver& resolve_label) {
    const auto& i = snapshot.record;
    const auto& key = snapshot.key;
    const auto q = DynamicMenu::quote;
    const int type = DynamicMenu::field(i, menu_field::TYPE),
              id = DynamicMenu::field(i, menu_field::ID);
    const std::string node = i.menu + "_" + std::to_string(id);
    const std::string base =
        std::string(settings.prefix) + "/menu/" + i.menu + "/" + std::to_string(id);
    const std::string availability = std::string(settings.prefix) + "/availability";
    const bool writable = snapshot.writable;
    const bool su = i.menu == "SU";
    std::string component, value;

    if (!su && type >= menu_type::NUMBER && type <= menu_type::OTHER_NUMBER && i.f.size() > 9) {
        int minimum, maximum;

        if (!DynamicMenu::integer(i.f[menu_field::MINIMUM], minimum) ||
            !DynamicMenu::integer(i.f[menu_field::MAXIMUM], maximum) || minimum > maximum) {
            return std::nullopt;
        }

        component = writable && settings.commands ? "number" : "sensor";
        int current;
        value =
            DynamicMenu::integer(i.f[menu_field::VALUE], current) ? std::to_string(current) : "";
    } else if (!su && type == menu_type::BOOLEAN && i.f.size() > 8) {
        component = writable && settings.commands ? "switch" : "sensor";
        value = i.f[menu_field::VALUE];
    } else if (!su && type == menu_type::CHOICE && i.f.size() > 10) {
        component = writable && settings.commands ? "select" : "sensor";
        value = i.f[menu_field::VALUE];
    } else if (su && type == overview_type::TEMPERATURE && i.f.size() > 6) {
        component = "sensor";
        int temperature;
        value = DynamicMenu::integer(i.f[overview_field::TEMPERATURE], temperature) &&
                        temperature != -2030 && temperature != -2000
                    ? std::to_string(temperature / 10.0f)
                    : "";
    } else if (su && type == overview_type::VERSION && i.f.size() > 4) {
        component = "sensor";
        value = i.f[overview_field::STATE_OR_VERSION];
    } else if (su && type == overview_type::STATE && i.f.size() > 4) {
        component = "sensor";
        value = resolve_label(DynamicMenu::field(i, overview_field::STATE_OR_VERSION));
    } else if (su && type == overview_type::VALVE && i.f.size() > 13) {
        component = "sensor";
        const int opening = DynamicMenu::field(i, overview_field::OPENING);
        value = opening >= 0 && opening <= 100 ? std::to_string(opening) : "";
    } else {
        return std::nullopt;
    }

    std::string name = resolve_label(DynamicMenu::field(i, su ? overview_field::DESCRIPTION
                                                              : menu_field::DESCRIPTION)) +
                       " · " + key;

    if (su && type == overview_type::VALVE) {
        name = resolve_label(3842) + " · " + key;
    }

    if (!su && type == menu_type::RAW_NUMBER) {
        name += " [raw]";
    }

    std::string d =
        "{\"name\":" + q(name) + ",\"unique_id\":" + q(identity + "_" + node) +
        ",\"state_topic\":" + q(base + "/state") +
        ",\"json_attributes_topic\":" + q(base + "/attributes") +
        ",\"availability_mode\":\"all\",\"availability\":[{\"topic\":" + q(availability) +
        "},{\"topic\":" + q(std::string(settings.prefix) + "/tech/availability") +
        "},{\"topic\":" + q(base + "/availability") +
        "}],\"enabled_by_default\":" + (i.menu == "MI" || i.menu == "MS" ? "false" : "true") +
        ",\"device\":{\"identifiers\":[" + q(identity) +
        "],\"name\":\"TECH RS bridge\",\"manufacturer\":\"Local TECH bridge\",\"sw_version\":" +
        q(build::SOFTWARE_VERSION) + "}";

    if (component != "sensor") {
        d += ",\"command_topic\":" + q(base + "/set") + ",\"retain\":false";
    }

    if (component == "number") {
        d += ",\"min\":" + std::to_string(DynamicMenu::field(i, menu_field::MINIMUM)) +
             ",\"max\":" + std::to_string(DynamicMenu::field(i, menu_field::MAXIMUM)) +
             ",\"step\":1,\"mode\":\"box\"";
    }

    if (component == "switch") {
        d += ",\"payload_on\":\"1\",\"payload_off\":\"0\"";
    }

    if (component == "select") {
        std::string options = "[", forward = "{", reverse = "{";
        bool first = true;

        for (size_t p = menu_field::OPTION_LABEL; p + 1 < i.f.size(); p += 2) {
            auto label = resolve_label(DynamicMenu::field(i, p)) + " [" + i.f[p + 1] + "]";

            if (!first) {
                options += ",";
                forward += ",";
                reverse += ",";
            }

            first = false;
            options += q(label);
            forward += q(i.f[p + 1]) + ":" + q(label);
            reverse += q(label) + ":" + q(i.f[p + 1]);
        }

        options += "]";
        forward += "}";
        reverse += "}";
        d += ",\"options\":" + options +
             ",\"value_template\":" + q("{{ " + forward + ".get(value, value) }}") +
             ",\"command_template\":" + q("{{ " + reverse + "[value] }}");
    }

    if (su && type == overview_type::TEMPERATURE) {
        d += ",\"device_class\":\"temperature\",\"unit_of_measurement\":\"°C\"";
    } else if (su && type == overview_type::VALVE) {
        d += ",\"unit_of_measurement\":\"%\",\"state_class\":\"measurement\"";
    } else if (!su && type == menu_type::NUMBER &&
               DynamicMenu::field(i, menu_field::UNIT) == 1035) {
        d += ",\"unit_of_measurement\":\"°C\"";
    } else if (!su && type == menu_type::NUMBER &&
               DynamicMenu::field(i, menu_field::UNIT) == 1044) {
        d += ",\"unit_of_measurement\":\"%\"";
    }

    d += "}";

    MqttProjection result;
    result.node = node;
    result.base = base;
    result.component = component;
    result.value = value;
    result.available = snapshot.visible && snapshot.fresh && !value.empty();
    result.attributes = "{\"menu\":" + q(i.menu) + ",\"id\":" + std::to_string(id) +
                        ",\"type\":" + std::to_string(type) +
                        ",\"visible\":" + (snapshot.visible ? "true" : "false") +
                        ",\"raw\":" + q(i.raw) + "}";
    result.discovery_topic = "homeassistant/" + component + "/" + identity + "/" + node + "/config";
    result.discovery = std::move(d);
    return result;
}

std::optional<OperationResult>
route_mqtt_command(ControllerService& service, const std::string& topic, const std::string& payload,
                   const MqttSettings& settings, bool retained, uint32_t now) {
    const std::string prefix = std::string(settings.prefix) + "/menu/";

    if (retained || topic.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    const auto parts = MenuText::split(topic.substr(prefix.size()), '/');

    if (parts.size() != 3 || parts[2] != "set") {
        return std::nullopt;
    }

    const auto key = parts[0] + ":" + parts[1];

    if (!settings.commands) {
        return OperationResult{ResultCode::CommandsDisabled, key};
    }

    return service.request_write(key, payload, std::nullopt, now);
}
} // namespace tech_rs_gateway
