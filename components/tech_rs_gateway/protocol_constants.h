// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>

namespace tech_rs_gateway {
/**
 * @brief Checks the item-ID range supported by command transmission.
 * @param id Controller menu item ID; zero is reserved for menu roots.
 * @return true for IDs 1–65535. Use for both UI capabilities and transport validation.
 */
inline bool valid_menu_id(int id) {
    return id > 0 && id <= 65535;
}

namespace protocol {
constexpr uint16_t INTERNET_ADDRESS = 0xfff4;
constexpr uint16_t CONTROLLER_TAG = 0x0400;
constexpr uint16_t MODULE_TAG = 0x1788;
constexpr uint16_t ANNOUNCE = 0x0101;
constexpr uint16_t DATA = 0x0301;
constexpr uint16_t IDLE = 0x0302;
constexpr uint16_t STARTUP_TRANSFER = 0x0001;
constexpr uint16_t TEXT_TRANSFER = 0x0049;
constexpr uint16_t BINARY_TRANSFER = 0x0050;
constexpr size_t MAX_FRAME_BYTES = 4096;
constexpr size_t MAX_TEXT_BYTES = 16384;
} // namespace protocol

namespace timing {
constexpr uint32_t FRAME_GAP_MS = 300;
constexpr uint32_t SESSION_TIMEOUT_MS = 15000;
constexpr uint32_t INITIALIZATION_TIMEOUT_MS = 90000;
constexpr uint32_t POLL_INTERVAL_MS = 10000;
constexpr uint32_t POLL_FRESH_MS = 30000;
constexpr uint32_t RECORD_FRESH_MS = 60000;
constexpr uint32_t WRITE_METADATA_FRESH_MS = 600000;
constexpr uint32_t TRANSFER_TIMEOUT_MS = 30000;
constexpr uint32_t READBACK_TIMEOUT_MS = 120000;
constexpr uint32_t SERVICE_INTERVAL_MS = 250;
constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;
constexpr uint32_t RECOVERY_INTERVAL_MS = 60000;
} // namespace timing

namespace menu_field {
constexpr size_t ID = 0;
constexpr size_t PARENT = 1;
constexpr size_t TYPE = 2;
constexpr size_t VISIBLE = 3;
constexpr size_t DESCRIPTION = 4;
constexpr size_t VALUE = 7;
constexpr size_t MINIMUM = 8;
constexpr size_t MAXIMUM = 9;
constexpr size_t UNIT = 11;
constexpr size_t ACTION_SUBTYPE = 8;
constexpr size_t OPTION_LABEL = 9;
constexpr size_t OPTION_VALUE = 10;
} // namespace menu_field

namespace limits {
constexpr size_t MAX_MENU_ITEMS = 512;
constexpr size_t MAX_RECORD_BYTES = 2048;
constexpr size_t MIN_MENU_HEAP_BYTES = 48000;
constexpr size_t RECOVERY_HEAP_BYTES = 80000;
constexpr size_t MENU_PAGE_SIZE = 24;
constexpr unsigned MAX_PARENT_DEPTH = 32;
constexpr unsigned MAX_RELOAD_ATTEMPTS = 2;
} // namespace limits

namespace menu_type {
constexpr int GROUP = 0;
constexpr int NUMBER = 1;
constexpr int RAW_NUMBER = 2;
constexpr int OTHER_NUMBER = 3;
constexpr int BOOLEAN = 10;
constexpr int CHOICE = 11;
constexpr int SCHEDULE = 12;
constexpr int ACTION = 20;
constexpr int CURVE = 30;
} // namespace menu_type

namespace overview_type {
constexpr int TEMPERATURE = 1;
constexpr int VALVE = 23;
constexpr int STATE = 40;
constexpr int VERSION = 50;
} // namespace overview_type

namespace overview_field {
constexpr size_t STATE_OR_VERSION = 4;
constexpr size_t DESCRIPTION = 5;
constexpr size_t TEMPERATURE = 6;
constexpr size_t OPENING = 9;
constexpr size_t PUMP = 10;
constexpr size_t TARGET = 13;
} // namespace overview_field
/**
 * @brief Application write lifecycle; Idle covers both no request and terminal outcomes recorded
 * separately.
 */
enum class WriteState { Idle, Pending, AwaitingReady, AwaitingAcknowledgment, AwaitingReadback };
} // namespace tech_rs_gateway
