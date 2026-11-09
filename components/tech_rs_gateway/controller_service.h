// SPDX-License-Identifier: MIT
#pragma once
#include "internet_session.h"
#include "menu_snapshot.h"

/** @brief Coordinates protocol and writes independently of HTTP and MQTT.
 * Snapshot/request methods lock internally and may be called by adapter threads.
 * Network/session lifecycle, receive() and tick() belong to the main loop.
 */
class ControllerService {
  public:
    /**
     * @brief Creates an independent controller service and connects its session callbacks.
     * @param available_heap Optional main-loop heap reader in bytes. Captured state must outlive
     * the service; an empty callback uses platform defaults.
     */
    explicit ControllerService(std::function<size_t()> available_heap = {});
    /**
     * @brief Accesses the gateway service used by production adapters.
     * @return Reference to the process-wide instance; do not delete it.
     */
    static ControllerService& instance();
    /**
     * @brief Updates the action-validation gate under the internal lock.
     * @param enabled Whether new and pending experimental actions may pass validation.
     * @note Does not execute an action or retract an already dispatched command.
     */
    void set_experimental_actions(bool enabled);
    /**
     * @brief Builds the session metadata from the gateway network configuration.
     * @param mac Six MAC-address octets.
     * @param ip Four IPv4 address octets.
     * @param mask Four subnet-mask octets.
     * @param gateway Four gateway-address octets.
     * @param dns Four DNS-server address octets.
     * @note Main-loop only. Supply correctly sized vectors; no connection is opened here.
     */
    void configure_network(const InternetSession::Bytes& mac, const InternetSession::Bytes& ip,
                           const InternetSession::Bytes& mask,
                           const InternetSession::Bytes& gateway,
                           const InternetSession::Bytes& dns);
    /**
     * @brief Begins a new session generation and cancels incomplete writes without replay.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @note Main-loop only. Stored records remain invalid until refreshed in the new session.
     */
    void restart(uint32_t now);
    /**
     * @brief Checks session health and reserves an eligible menu-recovery attempt.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @param heap Currently available heap in bytes.
     * @return true when the main loop should restart the session.
     * @note Main-loop only. An eligible memory-recovery check increments its bounded retry counter.
     */
    bool reconnect_due(uint32_t now, size_t heap);
    /**
     * @brief Processes a UART chunk and routes session events to the locked menu model.
     * @param bytes Received octets; chunks need not be complete frames.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @param send Synchronous packet writer; consume or copy bytes before returning.
     * @param log Synchronous diagnostic sink; consume or copy text during the callback.
     * @note Main-loop only. Callbacks must not re-enter the session.
     */
    void receive(const InternetSession::Bytes& bytes, uint32_t now,
                 const InternetSession::Sender& send, const InternetSession::Logger& log);
    /**
     * @brief Advances write dispatch, availability and readback deadlines.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @note Main-loop only; menu locking is internal. Call regularly even without received bytes.
     */
    void tick(uint32_t now);

    /**
     * @brief Copies connection and operation status under the internal lock.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @return Owned status data with poll age computed at now.
     * @note The online flag reflects the most recent tick(); this call does not advance the
     * session.
     */
    tech_rs_gateway::ControllerStatus snapshot(uint32_t now);
    /**
     * @brief Copies a bounded page of menu rows without exposing mutable model storage.
     * @param group Menu group (MU, MI, MS or SU); an unknown group produces an empty page.
     * @param offset Zero-based row offset within the group.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @return Owned rows, total count, next offset and status. At most limits::MENU_PAGE_SIZE rows
     * are copied.
     */
    tech_rs_gateway::MenuPage page(const std::string& group, size_t offset, uint32_t now);
    /**
     * @brief Copies the next record and its computed capabilities under the internal lock.
     * @param after Exclusive lexicographic menu-key cursor; an empty string starts iteration.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @return Owned record snapshot, or nullopt at the end. The caller can restart with an empty
     * cursor.
     */
    std::optional<tech_rs_gateway::ItemSnapshot> next_item(const std::string& after, uint32_t now);
    /**
     * @brief Validates and stages a write under the service's internal lock.
     * @param key Menu group and item ID, for example MU:101.
     * @param value Integer text in protocol units.
     * @param expected Previous value (or complete raw action record). MQTT scalar
     * commands may omit it for an atomic current-value check.
     * @param now Monotonic time in milliseconds.
     * @param action Request an experimental action rather than a scalar change.
     * @return Structured acceptance/error; acceptance is not controller readback.
     */
    tech_rs_gateway::OperationResult request_write(const std::string& key, const std::string& value,
                                                   const std::optional<std::string>& expected,
                                                   uint32_t now, bool action = false);

  private:
    DynamicMenu menu_;
    InternetSession session_;
    std::function<size_t()> available_heap_;
    tech_rs_gateway::ControllerStatus snapshot_locked(uint32_t now) const;
    void sent(int id, int value, uint32_t now);
    void acknowledged(int id, int value);
};
