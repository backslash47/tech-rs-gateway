// SPDX-License-Identifier: MIT
#include "menu_api.h"

namespace tech_rs_gateway {
bool MenuApi::authorized(const std::optional<std::string>& token,
                         const std::optional<std::string>& origin,
                         const std::optional<std::string>& host,
                         const std::string& expected_token) {
    return token && *token == expected_token && (!origin || (host && *origin == "http://" + *host));
}

ApiResponse MenuApi::menu(ControllerService& service, const std::string& group,
                          const std::string& offset, uint32_t now, const std::string& token,
                          size_t heap) {
    int start;

    if ((group != "MU" && group != "MI" && group != "MS" && group != "SU") ||
        !DynamicMenu::integer(offset, start) || start < 0) {
        return {400, OperationResult{ResultCode::InvalidRequest}.json()};
    }

    const auto page = service.page(group, static_cast<size_t>(start), now);
    const auto& state = page.status;
    const auto quote = DynamicMenu::quote;
    const auto boolean = [](bool value) {
        return value ? "true" : "false";
    };
    std::string body = "{\"online\":" + std::string(boolean(state.online)) +
                       ",\"experimental_actions\":" + boolean(state.experimental_actions) +
                       ",\"overflow\":" + boolean(state.overflow) +
                       ",\"busy\":" + boolean(state.busy()) + ",\"poll_age\":" +
                       (state.poll_age_ms ? std::to_string(*state.poll_age_ms / 1000) : "null") +
                       ",\"missing_items\":" + std::to_string(state.missing_items) +
                       ",\"heap_free\":" + std::to_string(heap) +
                       ",\"result\":" + state.result.json() + ",\"token\":" + quote(token) +
                       ",\"items\":[";
    bool first = true;

    for (const auto& row : page.rows) {
        if (!first) {
            body += ',';
        }

        first = false;
        body += "{\"key\":" + quote(row.key) + ",\"raw\":" + quote(row.raw) +
                ",\"age\":" + std::to_string(row.age_seconds) +
                ",\"visible\":" + boolean(row.visible) + ",\"writable\":" + boolean(row.writable) +
                ",\"action\":" + boolean(row.actionable) + "}";
    }

    body += "],\"total\":" + std::to_string(page.total) +
            ",\"next\":" + (page.next ? std::to_string(*page.next) : "null") + "}";
    return {200, std::move(body)};
}

ApiResponse MenuApi::write(ControllerService& service, bool authorized,
                           const std::optional<std::string>& key,
                           const std::optional<std::string>& value,
                           const std::optional<std::string>& expected, bool action, uint32_t now) {
    if (!authorized) {
        return {403, OperationResult{ResultCode::InvalidSession}.json()};
    }

    if (!key || !value || !expected) {
        return {400, OperationResult{ResultCode::MissingParameter}.json()};
    }

    const auto result = service.request_write(*key, *value, expected, now, action);
    return {result.accepted() ? 202 : 409, result.json()};
}
} // namespace tech_rs_gateway
