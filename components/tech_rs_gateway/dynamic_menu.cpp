// SPDX-License-Identifier: MIT
#include "dynamic_menu.h"
#include <charconv>
using namespace tech_rs_gateway;
#ifdef ESP_PLATFORM
#include "esp_system.h"
#endif

MenuFields::MenuFields(const std::vector<std::string>& fields) {
    size_t total = 0;

    for (const auto& s : fields) {
        total += s.size();
    }

    data.reserve(total);
    ends.reserve(fields.size());

    for (const auto& s : fields) {
        data += s;
        ends.push_back(uint16_t(data.size()));
    }
}

size_t MenuFields::size() const {
    return ends.size();
}

std::string MenuFields::operator[](size_t i) const {
    const size_t start = i ? ends[i - 1] : 0;
    return data.substr(start, ends[i] - start);
}

bool DynamicMenu::recovery_due(uint32_t now, size_t heap) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!overflow || heap < limits::RECOVERY_HEAP_BYTES || write_state != WriteState::Idle ||
        reload_attempts >= limits::MAX_RELOAD_ATTEMPTS ||
        now - last_reload < timing::RECOVERY_INTERVAL_MS) {
        return false;
    }

    ++reload_attempts;
    last_reload = now;
    return true;
}

unsigned DynamicMenu::menu_index(const std::string& s) {
    return s == "MU" ? 0 : s == "MI" ? 1 : s == "MS" ? 2 : 3;
}

size_t DynamicMenu::missing_count() const {
    size_t n = 0;

    for (const auto& v : missing) {
        n += v.size();
    }

    return n;
}

void DynamicMenu::update_overflow() {
    overflow = missing_count() > 0;

    for (bool v : missing_untracked) {
        overflow |= v;
    }
}

void DynamicMenu::mark_missing(unsigned group, int id) {
    auto& v = missing[group];

    if (std::find(v.begin(), v.end(), id) == v.end()) {
        if (v.size() < limits::MAX_MENU_ITEMS) {
            v.push_back(id);
        } else {
            missing_untracked[group] = true;
        }
    }

    update_overflow();
}

void DynamicMenu::recovered(unsigned group, int id) {
    auto& v = missing[group];
    v.erase(std::remove(v.begin(), v.end(), id), v.end());
    update_overflow();
}

void DynamicMenu::begin_session() {
    std::lock_guard<std::mutex> lock(mutex);
    ++generation;
    last_poll_ack = 0;
    online = false;

    if (write_state != WriteState::Idle) {
        write_state = WriteState::Idle;
        result = {ResultCode::Reconnected, key, value, true};
    }
}

void DynamicMenu::poll_ack(uint32_t now) {
    std::lock_guard<std::mutex> lock(mutex);
    last_poll_ack = now;
}

bool DynamicMenu::fresh(const Item& i, uint32_t now, uint32_t ttl) const {
    return i.generation == generation &&
           (now - i.seen < ttl || (last_poll_ack && now - last_poll_ack < timing::POLL_FRESH_MS));
}

bool DynamicMenu::integer(const std::string& s, int& v) {
    if (s.empty() || s.size() > 11) {
        return false;
    }

    size_t p = s[0] == '-' ? 1 : 0;

    if (p == s.size()) {
        return false;
    }

    for (size_t i = p; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }

    int parsed;
    const auto conversion = std::from_chars(s.data(), s.data() + s.size(), parsed);

    if (conversion.ec != std::errc{} || conversion.ptr != s.data() + s.size()) {
        return false;
    }

    v = parsed;
    return true;
}

int DynamicMenu::field(const Item& i, size_t n, int fallback) {
    int v;
    return n < i.f.size() && integer(i.f[n], v) ? v : fallback;
}

std::string DynamicMenu::quote(const std::string& s) {
    std::string r = "\"";

    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            r += '\\';
            r += c;
        } else if (c < 32) {
            char b[7];
            snprintf(b, sizeof(b), "\\u%04x", c);
            r += b;
        } else {
            r += c;
        }
    }

    return r + '"';
}

bool DynamicMenu::visible(const Item& i) const {
    const Item* p = &i;

    for (unsigned depth = 0; depth < limits::MAX_PARENT_DEPTH; ++depth) {
        if (field(*p, menu_field::VISIBLE) != 1) {
            return false;
        }

        int parent = field(*p, menu_field::PARENT);

        if (parent == 0) {
            return true;
        }

        auto it = items.find(i.menu + ":" + std::to_string(parent));

        if (it == items.end()) {
            return false;
        }

        p = &it->second;
    }

    return false;
}

bool DynamicMenu::writable(const Item& i) const {
    if (i.menu == "SU" || !valid_menu_id(field(i, menu_field::ID)) || !visible(i)) {
        return false;
    }

    int type = field(i, menu_field::TYPE);
    return (type >= menu_type::NUMBER && type <= menu_type::OTHER_NUMBER &&
            i.f.size() > menu_field::MAXIMUM) ||
           (type == menu_type::BOOLEAN && i.f.size() > menu_field::MINIMUM) ||
           (type == menu_type::CHOICE && i.f.size() > menu_field::OPTION_VALUE);
}

int DynamicMenu::action_value(const Item& i) {
    if (field(i, menu_field::TYPE) != menu_type::ACTION || i.f.size() < 10) {
        return -1;
    }

    switch (field(i, menu_field::ACTION_SUBTYPE)) {
    case 0:
        return 2;
    case 1:
        return 1;
    case 2:
        return 3;
    default:
        return -1;
    }
}

bool DynamicMenu::actionable(const Item& i) const {
    return experimental_actions && i.menu != "SU" && valid_menu_id(field(i, menu_field::ID)) &&
           visible(i) && action_value(i) >= 0;
}

ResultCode DynamicMenu::request_action(const std::string& k, const std::string& v,
                                       const std::string& expected, uint32_t now) {
    if (write_state != WriteState::Idle) {
        return ResultCode::Busy;
    }

    if (!online) {
        return ResultCode::Offline;
    }

    auto it = items.find(k);

    if (it == items.end()) {
        return ResultCode::MissingItem;
    }

    const auto& i = it->second;

    if (!fresh(i, now, timing::WRITE_METADATA_FRESH_MS)) {
        return ResultCode::StaleMetadata;
    }

    if (!actionable(i)) {
        return ResultCode::NotWritable;
    }

    if (expected != i.raw) {
        return ResultCode::Conflict;
    }

    int n;

    if (!integer(v, n) || n != action_value(i)) {
        return ResultCode::UnknownOption;
    }

    kind = WriteKind::Action;
    action_record = i.raw;
    key = k;
    id = field(i, menu_field::ID);
    value = n;
    requested_at = now;
    write_state = WriteState::Pending;
    result = {ResultCode::Queued, key, value, true};
    return ResultCode::Queued;
}

void DynamicMenu::acknowledge_action(int item, int sent_value) {
    if (kind == WriteKind::Action && write_state == WriteState::AwaitingAcknowledgment &&
        id == item && value == sent_value) {
        write_state = WriteState::Idle;
        result = {ResultCode::ActionDelivered, key, value, true};
    }
}

void DynamicMenu::consume(const std::string& text, uint32_t now, size_t available_heap) {
    if (text.size() < 2 || text.front() != '<' || text.back() != '>') {
        return;
    }

    auto b = MenuText::split(text.substr(1, text.size() - 2), '|');

    if (b.size() < 4 || (b[0] != "MU" && b[0] != "MI" && b[0] != "MS" && b[0] != "SU")) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex);
    const unsigned group = menu_index(b[0]);

    if (b[2] == "1") {
        missing[group].clear();
        missing_untracked[group] = false;
        update_overflow();
    }

    if (b[2] == "1") {
        for (auto it = items.begin(); it != items.end();) {
            if (it->second.menu == b[0]) {
                it = items.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (size_t n = 3; n < b.size(); ++n) {
        auto f = MenuText::split(b[n], ',');
        int item_id;

        if (f.size() < 4 || !integer(f[menu_field::ID], item_id) ||
            b[n].size() > limits::MAX_RECORD_BYTES) {
            continue;
        }

        auto k = b[0] + ":" + f[0];

        if (!items.count(k) && items.size() >= limits::MAX_MENU_ITEMS) {
            mark_missing(group, item_id);
            continue;
        }
#ifdef ESP_PLATFORM
        available_heap = esp_get_free_heap_size();
#endif
        if (!items.count(k) && available_heap < limits::MIN_MENU_HEAP_BYTES) {
            mark_missing(group, item_id);
            continue;
        }

        items[k] = {b[0], b[n], std::move(f), now, generation};
        recovered(group, item_id);

        int reported_value;

        if ((write_state == WriteState::AwaitingAcknowledgment ||
             write_state == WriteState::AwaitingReadback) &&
            kind == WriteKind::Scalar && k == key && items[k].f.size() > menu_field::VALUE &&
            integer(items[k].f[menu_field::VALUE], reported_value) && reported_value == value) {
            write_state = WriteState::Idle;
            result = {ResultCode::Confirmed, key, value, true};
        }
    }
}

ResultCode DynamicMenu::request(const std::string& k, const std::string& v,
                                const std::string& expected, uint32_t now) {
    if (write_state != WriteState::Idle) {
        return ResultCode::Busy;
    }

    if (!online) {
        return ResultCode::Offline;
    }

    auto it = items.find(k);

    if (it == items.end()) {
        return ResultCode::MissingItem;
    }

    const auto& i = it->second;

    if (!fresh(i, now, timing::WRITE_METADATA_FRESH_MS)) {
        return ResultCode::StaleMetadata;
    }

    if (!writable(i)) {
        return ResultCode::NotWritable;
    }

    if (expected != i.f[menu_field::VALUE]) {
        return ResultCode::Conflict;
    }

    int n;

    if (!integer(v, n)) {
        return ResultCode::InvalidInteger;
    }

    int t = field(i, menu_field::TYPE);

    if (t >= menu_type::NUMBER && t <= menu_type::OTHER_NUMBER &&
        (n < field(i, menu_field::MINIMUM) || n > field(i, menu_field::MAXIMUM))) {
        return ResultCode::OutOfRange;
    }

    if (t == menu_type::BOOLEAN && n != 0 && n != 1) {
        return ResultCode::InvalidBoolean;
    }

    if (t == menu_type::CHOICE) {
        bool found = false;

        for (size_t p = menu_field::OPTION_VALUE; p < i.f.size(); p += 2) {
            if (field(i, p) == n) {
                found = true;
            }
        }

        if (!found) {
            return ResultCode::UnknownOption;
        }
    }

    int item_id = field(i, menu_field::ID);
    kind = WriteKind::Scalar;
    action_record.clear();
    key = k;
    id = item_id;
    value = n;
    requested_at = now;
    write_state = WriteState::Pending;
    result = {ResultCode::Queued, key, value, true};
    return ResultCode::Queued;
}
