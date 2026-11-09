// SPDX-License-Identifier: MIT
#include "menu_mqtt.h"
#include "controller_service.h"
#include "esp_mac.h"
#include "esphome/components/json/json_util.h"
#include "esphome/core/hal.h"
#include "mqtt_labels.h"
#include "mqtt_projection.h"

MenuMqtt& MenuMqtt::instance() {
    static MenuMqtt m;
    return m;
}

MqttSettings MenuMqtt::configuration() {
    std::lock_guard<std::mutex> lock(mutex);
    return config;
}

void MenuMqtt::start() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char id[24];
    snprintf(id, sizeof(id), "tech_rs_%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    identity = id;
    pref = esphome::global_preferences->make_preference<MqttSettings>(0x74ec5051);

    if (!pref.load(&config) || !validate_mqtt(config).empty()) {
        config = MqttSettings{};
        snprintf(config.prefix, sizeof(config.prefix), "%s", id);
    }

    apply_pending = true;
}

std::string MenuMqtt::save(const MqttSettings& next) {
    const auto problem = validate_mqtt(next);

    if (!problem.empty()) {
        return problem;
    }

    std::lock_guard<std::mutex> lock(mutex);
    config = next;
    apply_pending = true;
    error.clear();
    return "";
}

std::string MenuMqtt::settings_json() {
    std::lock_guard<std::mutex> lock(mutex);
    const auto q = DynamicMenu::quote;
    return "{\"enabled\":" + std::string(config.enabled ? "true" : "false") +
           ",\"discovery\":" + (config.discovery ? "true" : "false") +
           ",\"commands\":" + (config.commands ? "true" : "false") + ",\"host\":" + q(config.host) +
           ",\"port\":" + std::to_string(config.port) + ",\"username\":" + q(config.username) +
           ",\"password_set\":" + (config.password[0] ? "true" : "false") +
           ",\"prefix\":" + q(config.prefix) + ",\"connected\":" + (connected ? "true" : "false") +
           ",\"applying\":" + (apply_pending ? "true" : "false") + ",\"error\":" + q(error) +
           ",\"published\":" + std::to_string(published.load()) +
           ",\"publish_failures\":" + std::to_string(publish_failures.load()) +
           ",\"last_publish\":" + q(last_publish) + "}";
}

void MenuMqtt::tick(uint32_t now) {
    bool apply = false;
    MqttSettings next;
    {
        std::lock_guard<std::mutex> lock(mutex);

        if (apply_pending) {
            next = config;
            apply_pending = false;
            apply = true;
        }
    }

    if (apply) {
        if (pref.save(&next) && esphome::global_preferences->sync()) {
            configure(next);
        } else {
            std::lock_guard<std::mutex> lock(mutex);
            error = "Cannot save settings";
            config = active;
        }
    }

    bool up, republish;
    std::deque<Command> work;
    {
        std::lock_guard<std::mutex> lock(mutex);
        up = connected;
        work.swap(commands);
        republish = republish_pending;
        republish_pending = false;
    }

    if (republish) {
        for (auto& kv : discovery_hash) {
            kv.second.hash = 0;
        }

        cursor.clear();
        last_result.clear();
        last_status = now - 5001;
    }

    if (!client || !up) {
        return;
    }

    for (const auto& c : work) {
        command(c, now);
    }

    // Do not advance the menu cursor while queued state/discovery messages drain.
    if (esp_mqtt_client_get_outbox_size(client) > 4096) {
        return;
    }

    auto& service = ControllerService::instance();
    const auto state = service.snapshot(now);
    const auto item = service.next_item(cursor, now);
    cursor = item ? item->key : "";
    const auto result = state.result.json();
    const bool online = state.online;

    if (now - last_status > 5000) {
        publish(std::string(active.prefix) + "/availability", "online", true);
        publish(std::string(active.prefix) + "/tech/availability", online ? "online" : "offline",
                true);

        if (result != last_result) {
            publish(std::string(active.prefix) + "/result", result, false);
            last_result = result;
        }

        last_status = now;

        for (auto it = discovery_hash.begin(); it != discovery_hash.end();) {
            if (now - it->second.last_seen > 600000) {
                const auto slash = it->first.find('_');
                const auto path = std::string(active.prefix) + "/menu/" +
                                  it->first.substr(0, slash) + "/" + it->first.substr(slash + 1);
                publish(path + "/availability", "offline", true);

                // Missing data must not erase entity IDs or user customizations in HA.

                it = discovery_hash.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (item) {
        publish_item(*item, now);
    }
}

uint32_t MenuMqtt::hash(const std::string& s) {
    uint32_t h = 2166136261u;

    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }

    return h;
}

bool MenuMqtt::publish(const std::string& topic, const std::string& payload, bool retain) {
    // Bounded nonblocking outbox; never stall UART waiting for the broker.
    if (!client || esp_mqtt_client_get_outbox_size(client) > 8192) {
        ++publish_failures;
        return false;
    }

    // PUBACK also wakes the MQTT task promptly; QoS0-only enqueue can be drained
    // at the socket read-timeout rate and starve later states behind metadata.
    const int result = esp_mqtt_client_enqueue(client, topic.c_str(), payload.data(),
                                               payload.size(), 1, retain, true);

    if (result < 0) {
        ++publish_failures;
        return false;
    }

    ++published;
    {
        std::lock_guard<std::mutex> lock(mutex);
        last_publish = topic;
    }

    return true;
}

void MenuMqtt::configure(const MqttSettings& next) {
    if (client) {
        // Queue offline status before stopping; broker will also send the LWT on an unclean
        // loss.
        esp_mqtt_client_publish(client, availability.c_str(), "offline", 0, 0, true);
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        connected = false;
        commands.clear();
    }

    active = next;
    cursor.clear();
    discovery_hash.clear();
    rx_expected = 0;
    rx_payload.clear();

    if (!active.enabled) {
        return;
    }

    uri = "mqtt://" + std::string(active.host);
    availability = std::string(active.prefix) + "/availability";
    esp_mqtt_client_config_t c{};
    c.broker.address.uri = uri.c_str();
    c.broker.address.port = active.port;
    c.credentials.client_id = identity.c_str();
    c.credentials.username = active.username;
    c.credentials.authentication.password = active.password;
    c.session.keepalive = 30;
    c.session.disable_clean_session = false;
    c.session.last_will.topic = availability.c_str();
    c.session.last_will.msg = "offline";
    c.session.last_will.msg_len = 7;
    c.session.last_will.qos = 1;
    c.session.last_will.retain = true;
    c.network.timeout_ms = 3000;
    c.network.reconnect_timeout_ms = 10000;
    c.buffer.size = 1024;
    c.buffer.out_size = 2048;
    client = esp_mqtt_client_init(&c);

    if (!client) {
        std::lock_guard<std::mutex> lock(mutex);
        error = "MQTT initialization failed";
        return;
    }

    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, &MenuMqtt::event, this);

    if (esp_mqtt_client_start(client) != ESP_OK) {
        std::lock_guard<std::mutex> lock(mutex);
        error = "MQTT start failed";
    }
}

void MenuMqtt::event(void* arg, esp_event_base_t, int32_t event_id, void* data) {
    static_cast<MenuMqtt*>(arg)->on_event(event_id, static_cast<esp_mqtt_event_handle_t>(data));
}

void MenuMqtt::on_event(int32_t event_id, esp_mqtt_event_handle_t e) {
    if (event_id == MQTT_EVENT_CONNECTED) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            connected = true;
            republish_pending = true;
            error.clear();
        }

        esp_mqtt_client_subscribe(e->client, (std::string(active.prefix) + "/menu/+/+/set").c_str(),
                                  0);
        esp_mqtt_client_enqueue(e->client, availability.c_str(), "online", 6, 1, true, true);
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        std::lock_guard<std::mutex> lock(mutex);
        connected = false;
    } else if (event_id == MQTT_EVENT_ERROR) {
        std::lock_guard<std::mutex> lock(mutex);
        error = "Broker connection/authentication failed";
    } else if (event_id == MQTT_EVENT_DATA) {
        if (e->current_data_offset == 0) {
            rx_expected = 0;
            rx_payload.clear();
            rx_topic.clear();

            if (e->retain || e->total_data_len < 1 || e->total_data_len > 64 ||
                e->topic_len > 200) {
                return;
            }

            rx_expected = e->total_data_len;
            rx_topic.assign(e->topic, e->topic_len);
        }

        if (!rx_expected || size_t(e->current_data_offset) != rx_payload.size() ||
            rx_payload.size() + e->data_len > rx_expected) {
            rx_expected = 0;
            return;
        }

        rx_payload.append(e->data, e->data_len);

        if (rx_payload.size() == rx_expected) {
            std::lock_guard<std::mutex> lock(mutex);

            if (commands.size() < 4) {
                commands.push_back({rx_topic, rx_payload});
            }

            rx_expected = 0;
        }
    }
}

void MenuMqtt::command(const Command& command, uint32_t now) {
    const auto result = tech_rs_gateway::route_mqtt_command(
        ControllerService::instance(), command.topic, command.payload, active, false, now);

    if (result) {
        publish(std::string(active.prefix) + "/result", result->json(), false);
    }
}

void MenuMqtt::publish_item(const tech_rs_gateway::ItemSnapshot& snapshot, uint32_t now) {
    const auto projected =
        tech_rs_gateway::project_mqtt_item(snapshot, active, identity, mqtt_menu_label);

    if (!projected) {
        const auto& record = snapshot.record;
        publish(std::string(active.prefix) + "/menu/" + record.menu + "/" +
                    std::to_string(DynamicMenu::field(record, tech_rs_gateway::menu_field::ID)) +
                    "/availability",
                "offline", true);
        return;
    }

    const auto& item = *projected;
    const auto& node = item.node;
    const auto& component = item.component;
    const bool visible = snapshot.visible;
    publish(item.base + "/attributes", item.attributes, true);
    publish(item.base + "/availability", item.available ? "online" : "offline", true);

    if (!item.value.empty()) {
        publish(item.base + "/state", item.value, true);
    }

    // One compact record per item, not four full discovery paths in RAM.
    auto& record = discovery_hash[node];
    record.last_seen = now;
    unsigned bit = 1;

    for (const char* kind : {"sensor", "number", "switch", "select"}) {
        if (!tech_rs_gateway::obsolete_discovery_type(kind, component, active.discovery)) {
            record.cleaned |= bit;
        } else if (!(record.cleaned & bit) && publish("homeassistant/" + std::string(kind) + "/" +
                                                          identity + "/" + node + "/config",
                                                      "", true)) {
            record.cleaned |= bit;
        }

        bit <<= 1;
    }

    if (record.cleaned != 15) {
        return;
    }

    if (!record.component.empty() &&
        tech_rs_gateway::obsolete_discovery_type(record.component, component, active.discovery)) {
        if (!publish("homeassistant/" + record.component + "/" + identity + "/" + node + "/config",
                     "", true)) {
            return;
        }

        record.component.clear();
        record.hash = 0;
    }

    if (!active.discovery || !visible) {
        return;
    }

    const auto fingerprint = hash(item.discovery);

    if (record.hash != fingerprint && publish(item.discovery_topic, item.discovery, true)) {
        record.hash = fingerprint;
        record.component = component;
    }
}
