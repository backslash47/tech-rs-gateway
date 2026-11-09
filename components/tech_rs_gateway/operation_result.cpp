// SPDX-License-Identifier: MIT
#include "operation_result.h"
#include "dynamic_menu.h"

namespace tech_rs_gateway {
const char* result_code(ResultCode code) {
    switch (code) {
    case ResultCode::None:
        return "idle";
    case ResultCode::Queued:
        return "write_queued";
    case ResultCode::Sent:
        return "write_sent";
    case ResultCode::Confirmed:
        return "write_confirmed";
    case ResultCode::ActionSent:
        return "action_sent";
    case ResultCode::ActionDelivered:
        return "action_delivered";
    case ResultCode::Busy:
        return "busy";
    case ResultCode::Offline:
        return "offline";
    case ResultCode::MissingItem:
        return "missing_item";
    case ResultCode::StaleMetadata:
        return "stale_metadata";
    case ResultCode::NotWritable:
        return "not_writable";
    case ResultCode::Conflict:
        return "value_conflict";
    case ResultCode::InvalidInteger:
        return "invalid_integer";
    case ResultCode::OutOfRange:
        return "out_of_range";
    case ResultCode::InvalidBoolean:
        return "invalid_boolean";
    case ResultCode::UnknownOption:
        return "unknown_option";
    case ResultCode::Cancelled:
        return "write_cancelled";
    case ResultCode::Reconnected:
        return "write_reconnected";
    case ResultCode::Timeout:
        return "write_timeout";
    case ResultCode::CommandsDisabled:
        return "commands_disabled";
    case ResultCode::InvalidSession:
        return "invalid_session";
    case ResultCode::MissingParameter:
        return "missing_parameter";
    case ResultCode::InvalidRequest:
        return "invalid_request";
    }

    return "invalid_request";
}

bool OperationResult::accepted() const {
    return code == ResultCode::Queued;
}

std::string OperationResult::json() const {
    return "{\"code\":" + DynamicMenu::quote(result_code(code)) +
           ",\"item\":" + DynamicMenu::quote(item) +
           ",\"value\":" + (has_value ? std::to_string(value) : "null") + "}";
}
} // namespace tech_rs_gateway
