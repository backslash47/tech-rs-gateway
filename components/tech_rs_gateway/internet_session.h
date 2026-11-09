// SPDX-License-Identifier: MIT
#pragma once
#include "protocol_constants.h"
#include "wire.h"
#include <functional>
#include <string>

/** @brief Single-threaded Internet-module session; use ControllerService for shared state. */
class InternetSession {
  public:
    using Bytes = tech_rs_gateway::Bytes;
    using Sender = std::function<void(const Bytes&)>;
    using Logger = std::function<void(const char*)>;
    /**
     * @brief Stages of one outgoing transfer; acknowledgment is not setting readback.
     */
    enum class TransferState { Idle, Pending, AwaitingReady, AwaitingAcknowledgment };

    /**
     * @brief Owned session flags and monotonic-millisecond timestamps; no references to buffers.
     */
    struct Snapshot {
        bool active, initialized, receiving;
        TransferState transfer;
        uint32_t started,
            last_frame; ///< Session start and last valid packet times, in milliseconds.
    };

    /**
     * @brief Synchronous session events delivered on the calling service-loop thread.
     * @note Callbacks must not re-enter or mutate the session. Text references are valid for the
     * callback; copy data that must be retained.
     */
    struct Callbacks {
        std::function<void(const std::string&, uint32_t)>
            text;                               ///< Complete block and reception time (ms).
        std::function<void(uint32_t)> poll_ack; ///< Validated poll completion time (ms).
        std::function<void(int, int, uint32_t)>
            sent;                                   ///< Item ID, value and transmission time (ms).
        std::function<void(int, int)> acknowledged; ///< Item ID and value; delivery, not execution.
        std::function<void()>
            restarted; ///< Controller bootstrap detected during an existing session.
    };

    /**
     * @brief Installs event sinks before starting a session.
     * @param callbacks Callbacks copied/moved into the session; empty individual sinks are allowed.
     * @note Call from the main loop before feed(); does not invoke callbacks immediately.
     */
    void set_callbacks(Callbacks callbacks);
    /**
     * @brief Builds emulated module metadata from local network identity.
     * @param mac Six MAC-address octets.
     * @param ip Four IPv4 address octets.
     * @param mask Four subnet-mask octets.
     * @param gateway Four gateway-address octets.
     * @param dns Four DNS-server address octets.
     * @note Does not validate vector sizes or open a connection. Metadata is retained across
     * start().
     */
    void configure(const Bytes& mac, const Bytes& ip, const Bytes& mask, const Bytes& gateway,
                   const Bytes& dns);
    /**
     * @brief Resets transfer state and starts a new initialization session.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @note Preserves configured metadata and callbacks, clears partial input and cancels queued
     * writes. No packet is emitted until feed().
     */
    void start(uint32_t now);
    /**
     * @brief Stops packet processing and clears outgoing transfer state.
     * @note Does not change controller settings; start() resets the remaining buffered/session
     * state.
     */
    void stop();
    /**
     * @brief Copies current session flags and timestamps.
     * @return Owned session snapshot; access from the same thread as feed().
     */
    Snapshot snapshot() const;
    /**
     * @brief Queues one previously validated controller command.
     * @param item Menu item ID in the range 1–65535.
     * @param value Requested integer in controller protocol units.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @return true if queued; false if disconnected, uninitialized, busy or the ID is invalid.
     * @note Bounds/visibility checks belong to ControllerService. A true result does not mean
     * transmitted or applied.
     */
    bool queue_menu(int item, int value, uint32_t now);
    /**
     * @brief Reassembles received chunks and processes complete packets through the shared codec.
     * @param bytes Received octets; may contain partial or multiple frames.
     * @param now Current monotonic time in milliseconds (uint32_t, not wall-clock time).
     * @param send Synchronous packet writer; copy data if retaining it beyond the callback.
     * @param log Synchronous diagnostic sink; text need only remain valid for the callback.
     * @note Single-threaded. Transport expiry stops the session; callbacks must not re-enter it.
     */
    void feed(const Bytes& bytes, uint32_t now, const Sender& send, const Logger& log);

  private:
    enum class InitStep { Preamble, EndPreamble, Announce, Metadata, EndMetadata, Idle };
    enum class ReadStep { Idle, AwaitingReady, AwaitingAcknowledgment };

    struct ReceiveResult {
        bool acknowledged = false, complete = false, initialized = false;
    };

    Callbacks callbacks_;
    tech_rs_gateway::FrameStream stream_;
    bool active_ = false, initialized_ = false, receiving_ = false, text_open_ = false;
    uint32_t started_ = 0, last_frame_ = 0, receive_progress_ = 0, last_read_ = 0, write_at_ = 0;
    InitStep init_step_ = InitStep::Preamble;
    ReadStep read_step_ = ReadStep::Idle;
    TransferState transfer_ = TransferState::Idle;
    bool read_ready_ = false;
    size_t text_count_ = 0;
    int write_item_ = 0, write_value_ = 0;
    std::string text_;
    Bytes metadata_;

    void respond(const tech_rs_gateway::Frame& frame, uint32_t now, const Sender& send,
                 const Logger& log);
    ReceiveResult receive(uint16_t opcode, const Bytes& data, uint32_t now, const Logger& log);
    Bytes next_message(const Bytes& route, const ReceiveResult& received, uint32_t now);
    Bytes initialization_message();
    Bytes read_message(const Bytes& route, uint32_t now);
    Bytes write_message(const Bytes& route, uint32_t now);
};
