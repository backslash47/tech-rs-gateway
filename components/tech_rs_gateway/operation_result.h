// SPDX-License-Identifier: MIT
#pragma once
#include <string>

namespace tech_rs_gateway {
/** @brief Stable operation codes shared by HTTP, MQTT and translated clients. */
enum class ResultCode {
    None,
    Queued,
    Sent,
    Confirmed,
    ActionSent,
    ActionDelivered,
    Busy,
    Offline,
    MissingItem,
    StaleMetadata,
    NotWritable,
    Conflict,
    InvalidInteger,
    OutOfRange,
    InvalidBoolean,
    UnknownOption,
    Cancelled,
    Reconnected,
    Timeout,
    CommandsDisabled,
    InvalidSession,
    MissingParameter,
    InvalidRequest
};

/** @brief Owned, machine-readable result. Item and value identify the operation. */
struct OperationResult {
    ResultCode code = ResultCode::None;
    std::string item;
    int value = 0;
    bool has_value = false;
    /**
     * @brief Checks acceptance of a submission, not eventual execution.
     * @return true only for ResultCode::Queued; false for all other codes, including later
     * confirmation events.
     */
    bool accepted() const;
    /**
     * @brief Serializes the stable code, item and optional value.
     * @return Owned JSON object; value is null when has_value is false.
     */
    std::string json() const;
};

/**
 * @brief Maps an operation enum to a stable language-independent API code.
 * @param code Operation code to encode.
 * @return Static string with process lifetime; an unrecognized enum maps to invalid_request.
 */
const char* result_code(ResultCode code);
} // namespace tech_rs_gateway
