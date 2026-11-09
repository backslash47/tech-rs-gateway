// SPDX-License-Identifier: MIT
#include "controller_service.h"

using namespace tech_rs_gateway;

ControllerService::ControllerService(std::function<size_t()> available_heap)
    : available_heap_(std::move(available_heap)) {
    session_.set_callbacks({[this](const std::string& text, uint32_t now) {
                                menu_.consume(text, now,
                                              available_heap_ ? available_heap_() : SIZE_MAX);
                            },
                            [this](uint32_t now) {
                                menu_.poll_ack(now);
                            },
                            [this](int id, int value, uint32_t now) {
                                sent(id, value, now);
                            },
                            [this](int id, int value) {
                                acknowledged(id, value);
                            },
                            [this] {
                                menu_.begin_session();
                            }});
}

ControllerService& ControllerService::instance() {
    static ControllerService service;
    return service;
}

void ControllerService::set_experimental_actions(bool enabled) {
    std::lock_guard<std::mutex> lock(menu_.mutex);
    menu_.experimental_actions = enabled;
}

void ControllerService::configure_network(const InternetSession::Bytes& mac,
                                          const InternetSession::Bytes& ip,
                                          const InternetSession::Bytes& mask,
                                          const InternetSession::Bytes& gateway,
                                          const InternetSession::Bytes& dns) {
    session_.configure(mac, ip, mask, gateway, dns);
}

void ControllerService::restart(uint32_t now) {
    menu_.begin_session();
    session_.start(now);
}

bool ControllerService::reconnect_due(uint32_t now, size_t heap) {
    const auto state = session_.snapshot();

    if (!state.active || now - state.last_frame > timing::SESSION_TIMEOUT_MS ||
        (!state.initialized && now - state.started > timing::INITIALIZATION_TIMEOUT_MS)) {
        return true;
    }

    return !state.receiving && state.transfer == InternetSession::TransferState::Idle &&
           menu_.recovery_due(now, heap);
}

void ControllerService::receive(const InternetSession::Bytes& bytes, uint32_t now,
                                const InternetSession::Sender& send,
                                const InternetSession::Logger& log) {
    session_.feed(bytes, now, send, log);
}

ControllerStatus ControllerService::snapshot_locked(uint32_t now) const {
    ControllerStatus status;
    status.online = menu_.online;
    status.experimental_actions = menu_.experimental_actions;
    status.overflow = menu_.overflow;
    status.write_state = menu_.write_state;
    status.missing_items = menu_.missing_count();
    status.result = menu_.result;

    if (menu_.last_poll_ack) {
        status.poll_age_ms = now - menu_.last_poll_ack;
    }

    return status;
}

ControllerStatus ControllerService::snapshot(uint32_t now) {
    std::lock_guard<std::mutex> lock(menu_.mutex);
    return snapshot_locked(now);
}

MenuPage ControllerService::page(const std::string& group, size_t offset, uint32_t now) {
    std::lock_guard<std::mutex> lock(menu_.mutex);
    MenuPage page;
    page.status = snapshot_locked(now);

    for (const auto& [key, item] : menu_.items) {
        if (item.menu != group) {
            continue;
        }

        if (page.total++ >= offset && page.rows.size() < limits::MENU_PAGE_SIZE) {
            page.rows.push_back({key, item.raw, (now - item.seen) / 1000, menu_.visible(item),
                                 menu_.writable(item), menu_.actionable(item)});
        }
    }

    if (offset < page.total && page.total - offset > page.rows.size()) {
        page.next = offset + page.rows.size();
    }

    return page;
}

std::optional<ItemSnapshot> ControllerService::next_item(const std::string& after, uint32_t now) {
    std::lock_guard<std::mutex> lock(menu_.mutex);
    const auto it = menu_.items.upper_bound(after);

    if (it == menu_.items.end()) {
        return std::nullopt;
    }

    const auto& item = it->second;
    return ItemSnapshot{it->first,
                        item,
                        menu_.visible(item),
                        menu_.writable(item),
                        menu_.actionable(item),
                        menu_.fresh(item, now)};
}

OperationResult ControllerService::request_write(const std::string& key, const std::string& value,
                                                 const std::optional<std::string>& expected,
                                                 uint32_t now, bool action) {
    std::lock_guard<std::mutex> lock(menu_.mutex);
    auto it = menu_.items.find(key);

    if (it == menu_.items.end()) {
        return {ResultCode::MissingItem, key};
    }

    if (action && !expected) {
        return {ResultCode::MissingParameter, key};
    }

    const auto previous = expected.value_or(
        it->second.f.size() > menu_field::VALUE ? it->second.f[menu_field::VALUE] : "");
    const auto code = action ? menu_.request_action(key, value, previous, now)
                             : menu_.request(key, value, previous, now);

    if (code == ResultCode::Queued) {
        return menu_.result;
    }

    return {code, key};
}

void ControllerService::sent(int id, int value, uint32_t now) {
    std::lock_guard<std::mutex> lock(menu_.mutex);

    if (menu_.write_state == WriteState::AwaitingReady && menu_.id == id && menu_.value == value) {
        menu_.write_state = WriteState::AwaitingAcknowledgment;
        menu_.sent_at = now;
        menu_.result = {menu_.kind == DynamicMenu::WriteKind::Action ? ResultCode::ActionSent
                                                                     : ResultCode::Sent,
                        menu_.key, value, true};
    }
}

void ControllerService::acknowledged(int id, int value) {
    std::lock_guard<std::mutex> lock(menu_.mutex);

    if (menu_.id != id || menu_.value != value ||
        menu_.write_state != WriteState::AwaitingAcknowledgment) {
        return;
    }

    if (menu_.kind == DynamicMenu::WriteKind::Action) {
        menu_.acknowledge_action(id, value);
    } else {
        menu_.write_state = WriteState::AwaitingReadback;
    }
}

void ControllerService::tick(uint32_t now) {
    const auto session = session_.snapshot();
    std::lock_guard<std::mutex> lock(menu_.mutex);
    menu_.online = session.active && session.initialized &&
                   now - session.last_frame < timing::SESSION_TIMEOUT_MS;

    if (menu_.write_state == WriteState::Pending) {
        auto it = menu_.items.find(menu_.key);
        const bool valid =
            it != menu_.items.end() &&
            (menu_.kind == DynamicMenu::WriteKind::Action
                 ? (menu_.actionable(it->second) && it->second.raw == menu_.action_record)
                 : menu_.writable(it->second));

        if (!menu_.online || !valid) {
            menu_.write_state = WriteState::Idle;
            menu_.result = {ResultCode::Cancelled, menu_.key, menu_.value, true};
        } else if (session_.queue_menu(menu_.id, menu_.value, now)) {
            menu_.write_state = WriteState::AwaitingReady;
        }
    }

    if (menu_.write_state != WriteState::Idle &&
        now - menu_.requested_at > timing::READBACK_TIMEOUT_MS) {
        menu_.write_state = WriteState::Idle;
        menu_.result = {ResultCode::Timeout, menu_.key, menu_.value, true};
    } else if (menu_.write_state == WriteState::AwaitingReady &&
               session.transfer == InternetSession::TransferState::Idle && !session.active) {
        menu_.write_state = WriteState::Idle;
        menu_.result = {ResultCode::Reconnected, menu_.key, menu_.value, true};
    }
}
