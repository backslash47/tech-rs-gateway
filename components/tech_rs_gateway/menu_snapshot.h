// SPDX-License-Identifier: MIT
#pragma once
#include "dynamic_menu.h"
#include "operation_result.h"
#include "protocol_constants.h"
#include <optional>

namespace tech_rs_gateway {
/** @brief Owned connection/write state; no pointers into mutable menu storage. */
struct ControllerStatus {
    bool online = false;
    bool experimental_actions = false;
    bool overflow = false;
    WriteState write_state = WriteState::Idle;
    size_t missing_items = 0;
    std::optional<uint32_t> poll_age_ms;
    OperationResult result;

    /**
     * @brief Checks whether a write is still in progress.
     * @return true for a non-Idle write state; a completed result may remain present when false.
     */
    bool busy() const {
        return write_state != WriteState::Idle;
    }
};

/** @brief Read-only copy of a record and its computed capabilities. */
struct ItemSnapshot {
    std::string key;
    DynamicMenu::Item record;
    bool visible = false, writable = false, actionable = false, fresh = false;
};

/** @brief Compact HTTP row; field arrays stay out of paginated snapshot copies. */
struct MenuRow {
    std::string key, raw;
    uint32_t age_seconds = 0;
    bool visible = false, writable = false, actionable = false;
};

/**
 * @brief Owns one bounded page of rows plus status and continuation metadata.
 */
struct MenuPage {
    ControllerStatus status;
    std::vector<MenuRow> rows;
    size_t total = 0;
    std::optional<size_t> next;
};
} // namespace tech_rs_gateway
