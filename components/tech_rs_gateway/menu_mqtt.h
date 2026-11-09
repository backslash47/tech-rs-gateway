#pragma once
#include "dynamic_menu.h"
#include "esphome/core/preferences.h"
#include "menu_snapshot.h"
#include "mqtt_client.h"
#include "mqtt_settings.h"
#include <atomic>
#include <deque>

/**
 * @brief Bridges the dynamic controller menu to MQTT and Home Assistant Discovery.
 * The main loop owns client lifecycle and publishing. HTTP and MQTT event threads
 * stage work through locked private state. Public methods acquire their own
 * locks; configuration() returns a copy rather than mutable shared storage.
 */
class MenuMqtt {
  private:
    /// Owned topic and payload copied from the MQTT event thread's receive buffer.
    struct Command {
        std::string topic, payload;
    };

    std::mutex mutex;
    MqttSettings config;
    bool connected = false, apply_pending = false, republish_pending = false;
    std::string error;
    std::atomic<uint32_t> published{0}, publish_failures{0};
    std::string last_publish;

  public:
    /**
     * @brief Copies current broker settings under the internal lock.
     * @return Owned complete settings, including the stored password; do not expose this object in
     * responses.
     */
    MqttSettings configuration();
    /**
     * @brief Accesses the runtime MQTT adapter.
     * @return Reference to the process-wide instance; do not delete it.
     */
    static MenuMqtt& instance();

    /// Loads stored configuration and derives identity; call once from the main loop.
    void start();

    /**
     * @brief Validates and stages settings from the HTTP thread.
     * @param next Full configuration, including the password to retain or replace.
     * @return Empty string on acceptance, otherwise a validation error.
     * @note Acquires mutex. NVS persistence and client reconfiguration occur
     * later in tick(); acceptance is not proof of connection or successful persistence.
     */
    std::string save(const MqttSettings& next);

    /**
     * @brief Serializes a locked settings and connection-status snapshot.
     * @return Owned JSON with password_set instead of the password value.
     */
    std::string settings_json();

    /** @brief Applies staged settings, processes commands and publishes bounded batches.
     * @param now Current monotonic time in milliseconds.
     * @note Call from the main loop without holding mutex.
     */
    void tick(uint32_t now);

  private:
    esphome::ESPPreferenceObject pref;
    esp_mqtt_client_handle_t client = nullptr;
    MqttSettings active;
    std::string identity, uri, availability, cursor, last_result;
    uint32_t last_status = 0;
    std::deque<Command> commands;
    std::string rx_topic, rx_payload;
    size_t rx_expected = 0;

    struct Discovery {
        uint32_t hash = 0, last_seen = 0;
        uint8_t cleaned = 0;
        std::string component;
    };

    std::map<std::string, Discovery> discovery_hash;

    static uint32_t hash(const std::string& s);

    bool publish(const std::string& topic, const std::string& payload, bool retain);

    void configure(const MqttSettings& next);

    static void event(void* arg, esp_event_base_t, int32_t event_id, void* data);

    void on_event(int32_t event_id, esp_mqtt_event_handle_t e);

    void command(const Command& c, uint32_t now);

    void publish_item(const tech_rs_gateway::ItemSnapshot& snapshot, uint32_t now);
};
