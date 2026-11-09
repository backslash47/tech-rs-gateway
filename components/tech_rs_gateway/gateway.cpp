// SPDX-License-Identifier: MIT
#include "gateway.h"
#include "controller_service.h"
#include "dynamic_menu_web.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/log.h"
#include "menu_mqtt.h"

TechGateway& TechGateway::instance() {
    static TechGateway gateway;
    return gateway;
}

void TechGateway::start(esphome::uart::UARTComponent* uart, bool experimental_actions) {
    uart_ = uart;
    ControllerService::instance().set_experimental_actions(experimental_actions);
    DynamicMenuWeb::instance().start();
    MenuMqtt::instance().start();
}

void TechGateway::loop() {
    if (!uart_) {
        return;
    }

    const uint32_t now = esphome::millis();
    auto& controller = ControllerService::instance();
    InternetSession::Bytes bytes;
    bytes.reserve(256);
    uint8_t byte;

    while (bytes.size() < 256 && uart_->available() && uart_->read_byte(&byte)) {
        bytes.push_back(byte);
    }

    if (!bytes.empty()) {
        controller.receive(
            bytes, now,
            [this](const InternetSession::Bytes& out) {
                uart_->write_array(out.data(), out.size());
            },
            [this](const char* status) {
                if (last_status_ != status) {
                    last_status_ = status;
                    ESP_LOGD("tech_rs", "%s", status);
                }
            });
    }

    if (now - last_service_ >= tech_rs_gateway::timing::SERVICE_INTERVAL_MS) {
        last_service_ = now;
        controller.tick(now);
        MenuMqtt::instance().tick(now);
    }

    if (now - last_reconnect_ < tech_rs_gateway::timing::RECONNECT_INTERVAL_MS) {
        return;
    }

    last_reconnect_ = now;

    if (!esphome::wifi::global_wifi_component->is_connected()) {
        return;
    }

    if (!controller.reconnect_due(now, esp_get_free_heap_size())) {
        return;
    }

    reconnect(now);
}

void TechGateway::reconnect(uint32_t now) {
    auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info{};
    esp_netif_dns_info_t dns{};
    uint8_t mac[6]{};

    if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK ||
        esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK ||
        dns.ip.type != ESP_IPADDR_TYPE_V4 || esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK) {
        return;
    }

    const auto octets = [](uint32_t value) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
        return InternetSession::Bytes(bytes, bytes + 4);
    };
    auto& controller = ControllerService::instance();
    controller.configure_network(InternetSession::Bytes(mac, mac + 6), octets(info.ip.addr),
                                 octets(info.netmask.addr), octets(info.gw.addr),
                                 octets(dns.ip.u_addr.ip4.addr));
    controller.restart(now);
    ESP_LOGI("tech_rs", "Starting Internet-module session");
}
