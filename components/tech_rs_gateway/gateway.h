// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>

namespace esphome::uart {
class UARTComponent;
}

/// ESPHome adapter coordinating UART, session recovery, the web handler and MQTT.
class TechGateway {
  public:
    /**
     * @brief Accesses the ESPHome gateway adapter.
     * @return Reference to the process-wide instance; do not delete it.
     */
    static TechGateway& instance();

    /**
     * @brief Connects subsystem callbacks and starts local services.
     * @param uart Initialized UART component that outlives the gateway; non-owning pointer.
     * @param experimental_actions Enable web-confirmed experimental action writes.
     * @note Call once at boot from the ESPHome main loop; does not change a heating target.
     */
    void start(esphome::uart::UARTComponent* uart, bool experimental_actions);

    /// Services received bytes, pending writes and reconnects; call regularly on the main loop.
    void loop();

  private:
    esphome::uart::UARTComponent* uart_ = nullptr;
    uint32_t last_service_ = 0, last_reconnect_ = 0;
    std::string last_status_;

    void reconnect(uint32_t now);
};
