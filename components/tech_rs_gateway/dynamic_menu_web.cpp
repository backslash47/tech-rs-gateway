// SPDX-License-Identifier: MIT
#include "dynamic_menu_web.h"
#include "controller_service.h"
#include "dynamic_menu.h"
#include "dynamic_menu_assets.h"
#include "esp_random.h"
#include "label_dictionary_data.h"
#include "menu_api.h"
#include "menu_mqtt.h"

namespace {
std::string request_path(AsyncWebServerRequest* request) {
    char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
    return std::string(request->url_to(buffer));
}
} // namespace

void DynamicMenuWeb::close_connection(AsyncWebServerRequest* r) {
    httpd_req_t* req = *r;
    httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
}

void DynamicMenuWeb::reply(AsyncWebServerRequest* r, int code, const char* type,
                           const std::string& body) {
    // ESPHome's IDF wrapper only maps 200/404/409; explicitly set other statuses.
    auto* res = r->beginResponse(200, type, body);
    const char* status = code == 202   ? "202 Accepted"
                         : code == 400 ? "400 Bad Request"
                         : code == 403 ? "403 Forbidden"
                         : code == 405 ? "405 Method Not Allowed"
                         : code == 409 ? "409 Conflict"
                                       : "200 OK";
    httpd_resp_set_status(*r, status);
    res->addHeader("Cache-Control", "no-store");
    res->addHeader("Connection", "close");
    r->send(res);
    close_connection(r);
}

DynamicMenuWeb& DynamicMenuWeb::instance() {
    static DynamicMenuWeb w;
    return w;
}

void DynamicMenuWeb::start() {
    char token[33];
    snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random(), (unsigned long)esp_random());
    token_ = token;
    auto* base = esphome::web_server_base::global_web_server_base;
    base->add_handler(this);
    base->init();
}

bool DynamicMenuWeb::canHandle(AsyncWebServerRequest* r) const {
    const auto u = request_path(r);
    return u == "/" || u == "/api/menu" || u == "/api/write" || u == "/labels.gz" ||
           u == "/api/mqtt";
}

void DynamicMenuWeb::handleRequest(AsyncWebServerRequest* r) {
    const auto path = request_path(r);

    if (path == "/api/mqtt") {
        handle_mqtt(r);
        return;
    }

    if (r->method() == HTTP_GET && (path == "/" || path == "/labels.gz")) {
        serve_assets(r);
        return;
    }

    const auto parameter = [r](const char* name) -> std::optional<std::string> {
        if (!r->hasParam(name)) {
            return std::nullopt;
        }

        return r->getParam(name)->value();
    };
    auto& service = ControllerService::instance();

    if (path == "/api/menu" && r->method() == HTTP_GET) {
        const auto response = tech_rs_gateway::MenuApi::menu(
            service, parameter("group").value_or("MU"), parameter("offset").value_or("0"),
            esphome::millis(), token_, esp_get_free_heap_size());
        reply(r, response.status, "application/json", response.body);
        return;
    }

    if (path == "/api/write" && r->method() == HTTP_POST) {
        const bool authorized = tech_rs_gateway::MenuApi::authorized(
            r->get_header("X-Tech-Token"), r->get_header("Origin"), r->get_header("Host"), token_);
        const auto response = tech_rs_gateway::MenuApi::write(
            service, authorized, parameter("key"), parameter("value"), parameter("expected"),
            parameter("action") == "1", esphome::millis());
        reply(r, response.status, "application/json", response.body);
        return;
    }

    reply(r, 405, "text/plain", "Method not allowed");
}

void DynamicMenuWeb::handle_mqtt(AsyncWebServerRequest* r) {

    if (r->method() == HTTP_GET) {
        reply(r, 200, "application/json", MenuMqtt::instance().settings_json());
        return;
    }

    if (r->method() != HTTP_POST) {
        reply(r, 405, "text/plain");
        return;
    }

    auto token = r->get_header("X-Tech-Token"), origin = r->get_header("Origin"),
         host = r->get_header("Host");

    if (!tech_rs_gateway::MenuApi::authorized(token, origin, host, token_)) {
        reply(r, 403, "application/json", "{\"error\":\"Invalid session\"}");
        return;
    }

    const size_t size = r->contentLength();

    if (!size || size > 2048) {
        reply(r, 400, "application/json", "{\"error\":\"Invalid body size\"}");
        return;
    }

    std::string body(size, '\0');
    size_t read = 0;

    while (read < size) {
        int n = httpd_req_recv(*r, &body[read], size - read);

        if (n <= 0) {
            reply(r, 400, "application/json", "{\"error\":\"Incomplete request\"}");
            return;
        }

        read += n;
    }

    MqttSettings next = MenuMqtt::instance().configuration();

    const bool valid = esphome::json::parse_json(body, [&](JsonObject obj) {
        if (!obj["enabled"].is<bool>() || !obj["commands"].is<bool>() ||
            !obj["discovery"].is<bool>() || !obj["port"].is<int>()) {
            return false;
        }

        const int port = obj["port"].as<int>();

        if (port < 1 || port > 65535) {
            return false;
        }

        next.enabled = obj["enabled"].as<bool>();
        next.commands = obj["commands"].as<bool>();
        next.discovery = obj["discovery"].as<bool>();
        next.port = port;
        auto copy = [&](const char* key, char* target, size_t cap) {
            if (!obj[key].is<const char*>()) {
                return false;
            }

            auto value = obj[key].as<const char*>();

            if (strlen(value) >= cap) {
                return false;
            }

            strcpy(target, value);
            return true;
        };

        if (!copy("host", next.host, sizeof(next.host)) ||
            !copy("username", next.username, sizeof(next.username)) ||
            !copy("prefix", next.prefix, sizeof(next.prefix))) {
            return false;
        }

        if (obj["clear_password"].is<bool>() && obj["clear_password"].as<bool>()) {
            next.password[0] = 0;
        }

        if (obj["password"].is<const char*>() && strlen(obj["password"].as<const char*>()) > 0 &&
            !copy("password", next.password, sizeof(next.password))) {
            return false;
        }

        return true;
    });
    auto error = valid ? MenuMqtt::instance().save(next) : "Invalid settings";
    reply(r, error.empty() ? 202 : 400, "application/json",
          "{\"error\":" + DynamicMenu::quote(error) + "}");
    return;
}

void DynamicMenuWeb::serve_assets(AsyncWebServerRequest* r) {
    const auto u = request_path(r);

    const uint8_t* data = menu_html_gz;
    size_t size = sizeof(menu_html_gz);
    const tech_rs_gateway::LabelBlock* blocks = nullptr;
    size_t block_count = 0;

    if (u == "/labels.gz") {
        const auto language = r->hasParam("lang") ? r->getParam("lang")->value() : "cs";

        if (language == "cs") {
            data = menu_labels_cs_gz;
            size = sizeof(menu_labels_cs_gz);
            blocks = label_blocks_cs;
            block_count = sizeof(label_blocks_cs) / sizeof(label_blocks_cs[0]);
        } else if (language == "pl") {
            data = menu_labels_pl_gz;
            size = sizeof(menu_labels_pl_gz);
            blocks = label_blocks_pl;
            block_count = sizeof(label_blocks_pl) / sizeof(label_blocks_pl[0]);
        } else if (language == "en") {
            data = menu_labels_en_gz;
            size = sizeof(menu_labels_en_gz);
            blocks = label_blocks_en;
            block_count = sizeof(label_blocks_en) / sizeof(label_blocks_en[0]);
        } else {
            reply(r, 400, "application/json", "{\"error\":\"Unsupported language\"}");
            return;
        }
    }

    auto* res = r->beginResponse(
        200, u == "/" ? "text/html; charset=utf-8" : "application/octet-stream", data, size);

    // ESP-IDF retains header pointers until send(); keep the storage alive throughout it.
    std::string lengths;

    if (blocks) {

        for (size_t index = 0; index < block_count; ++index) {
            if (index != 0) {
                lengths += ',';
            }

            lengths += std::to_string(blocks[index].compressed_size);
        }

        res->addHeader("X-Tech-Dictionary-Blocks", lengths.c_str());
    } else {
        res->addHeader("Content-Encoding", "gzip");
    }

    res->addHeader("Cache-Control", "no-cache");
    res->addHeader("Connection", "close");
    r->send(res);
    close_connection(r);
    return;
}
