// SPDX-License-Identifier: MIT
#pragma once
#include "menu_text.h"
#include "operation_result.h"
#include "protocol_constants.h"
#include <algorithm>
#include <array>
#include <climits>
#include <mutex>

/** @brief Compact CSV fields; returned values are owned copies. */
class MenuFields {
  public:
    /**
     * @brief Creates empty compact field storage.
     */
    MenuFields() = default;
    /**
     * @brief Copies parsed CSV fields into compact owned storage.
     * @param fields Fields in order, including empty strings; their combined byte length must fit
     * uint16_t.
     */
    MenuFields(const std::vector<std::string>& fields);
    /**
     * @brief Reports the number of stored fields.
     * @return Field count, including empty fields.
     */
    size_t size() const;
    /**
     * @brief Copies one field out of compact storage.
     * @param i Zero-based field index; must be less than size().
     * @return Owned field string; no bounds checking is performed.
     */
    std::string operator[](size_t i) const;

  private:
    std::string data;
    std::vector<uint16_t> ends;
};

class ControllerService;

/** @brief Private menu/write model owned by ControllerService.
 * Public ingestion methods lock internally. All other mutable access is confined
 * to ControllerService, which holds mutex while taking snapshots or staging work.
 */
class DynamicMenu {
  public:
    /**
     * @brief Owns a controller record, parsed fields and session-scoped reception metadata.
     */
    struct Item {
        std::string menu, raw;
        MenuFields f;
        uint32_t seen = 0, generation = 0;
    };

    /**
     * @brief Parses a complete signed decimal value within the C++ int range.
     * @param s Input text without whitespace or numeric suffixes.
     * @param v Output value, modified only on successful parsing.
     * @return true on success; false for malformed or out-of-range input.
     */
    static bool integer(const std::string& s, int& v);
    /**
     * @brief Reads an integer field from an owned controller record.
     * @param i Record to read without mutation.
     * @param n Zero-based CSV field index.
     * @param fallback Value returned when the field is absent or not a valid integer.
     * @return Parsed integer or fallback.
     */
    static int field(const Item& i, size_t n, int fallback = -1);
    /**
     * @brief Encodes text as a quoted JSON string.
     * @param s Input bytes; UTF-8 text is retained and controls are escaped.
     * @return Owned JSON string literal including surrounding quotes.
     */
    static std::string quote(const std::string& s);
    /**
     * @brief Applies received menu records and checks fresh scalar write readback.
     * @param text Complete controller text block, including angle brackets.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @param available_heap Available heap bytes for host tests. ESP builds read the platform heap
     * for each record; SIZE_MAX is unrestricted on the host.
     * @note Locks internally. Malformed or unsupported groups are ignored; capacity failures record
     * missing IDs instead of inventing values.
     */
    void consume(const std::string& text, uint32_t now, size_t available_heap = SIZE_MAX);
    /**
     * @brief Invalidates prior-generation records and cancels any unfinished write.
     * @note Locks internally. Does not erase the record map or transmit a command.
     */
    void begin_session();
    /**
     * @brief Records the time of a validated read handshake.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @note Locks internally. The caller must not invoke this for ordinary idle traffic.
     */
    void poll_ack(uint32_t now);
    /**
     * @brief Reserves a rate-limited recovery attempt when memory and state permit it.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @param heap Available heap in bytes.
     * @return true after reserving an attempt; false otherwise.
     * @note Locks internally and increments the retry counter on success; this is not a pure query.
     */
    bool recovery_due(uint32_t now, size_t heap);

  private:
    friend class ControllerService;
    enum class WriteKind { Scalar, Action };
    std::mutex mutex;
    std::map<std::string, Item> items;
    std::array<std::vector<int>, 4> missing;
    std::array<bool, 4> missing_untracked{};
    uint32_t last_reload = 0, generation = 0, last_poll_ack = 0;
    unsigned reload_attempts = 0;
    bool online = false, experimental_actions = false, overflow = false;
    tech_rs_gateway::WriteState write_state = tech_rs_gateway::WriteState::Idle;
    WriteKind kind = WriteKind::Scalar;
    int id = 0, value = 0;
    uint32_t requested_at = 0, sent_at = 0;
    std::string key, action_record;
    tech_rs_gateway::OperationResult result;

    static unsigned menu_index(const std::string& s);
    size_t missing_count() const;
    void update_overflow();
    void mark_missing(unsigned group, int id);
    void recovered(unsigned group, int id);
    bool fresh(const Item& i, uint32_t now,
               uint32_t ttl = tech_rs_gateway::timing::RECORD_FRESH_MS) const;
    bool visible(const Item& i) const;
    bool writable(const Item& i) const;
    static int action_value(const Item& i);
    bool actionable(const Item& i) const;
    tech_rs_gateway::ResultCode request_action(const std::string& k, const std::string& v,
                                               const std::string& expected, uint32_t now);
    tech_rs_gateway::ResultCode request(const std::string& k, const std::string& v,
                                        const std::string& expected, uint32_t now);
    void acknowledge_action(int item, int sent_value);
};
