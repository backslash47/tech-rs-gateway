// SPDX-License-Identifier: MIT
#include "internet_session.h"
#include "st505_profile.h"
#include <algorithm>

using namespace tech_rs_gateway;

namespace {
bool ready(const Bytes& route) {
    return route == Bytes({2, 1, 0xff, 0xff});
}

bool acknowledged(const Bytes& route, size_t length) {
    return route.size() == 6 && route[0] == 4 && word(&route[1]) == length && route[3] == 3 &&
           route[4] == 0xff && route[5] == 0xff;
}

Bytes announcement(uint16_t type, size_t length) {
    Bytes bytes{1, 1, 0, 0};
    append_word(bytes, type);
    append_word(bytes, static_cast<uint16_t>(length));
    return bytes;
}
} // namespace

void InternetSession::set_callbacks(Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
}

void InternetSession::configure(const Bytes& mac, const Bytes& ip, const Bytes& mask,
                                const Bytes& gateway, const Bytes& dns) {
    metadata_ = {3, 1};
    auto add_field = [&](uint16_t tag, const Bytes& value) {
        metadata_.insert(metadata_.end(),
                         {uint8_t(value.size() + 2), uint8_t(tag), uint8_t(tag >> 8)});
        metadata_.insert(metadata_.end(), value.begin(), value.end());
    };
    auto add_fixed_field = [&](uint16_t tag, const auto& value) {
        add_field(tag, Bytes(value.begin(), value.end()));
    };
    namespace profile = st505_profile;
    add_fixed_field(profile::tag::VERSION, profile::VERSION_BYTES);
    Bytes identity = mac;
    identity.resize(profile::IDENTITY_BYTES, 0);
    add_field(profile::tag::IDENTITY, identity);
    add_fixed_field(profile::tag::COMPATIBILITY_0028, profile::COMPATIBILITY_0028_BYTES);
    add_fixed_field(profile::tag::MODULE_MODEL, profile::MODULE_MODEL_BYTES);
    add_fixed_field(profile::tag::COMPATIBILITY_001E, profile::COMPATIBILITY_001E_BYTES);
    add_field(profile::tag::MAC_SUFFIX,
              mac.size() == profile::MAC_BYTES
                  ? Bytes(mac.begin() + profile::MAC_SUFFIX_OFFSET, mac.end())
                  : Bytes(profile::MAC_SUFFIX_BYTES, 0));
    add_fixed_field(profile::tag::DHCP_RELATED, profile::DHCP_RELATED_BYTES);
    add_field(profile::tag::IPV4_ADDRESS, ip);
    add_field(profile::tag::SUBNET_MASK, mask);
    add_field(profile::tag::GATEWAY, gateway);
    add_field(profile::tag::DNS, dns);
    add_fixed_field(profile::tag::COMPATIBILITY_000A, profile::COMPATIBILITY_000A_BYTES);
}

void InternetSession::start(uint32_t now) {
    active_ = true;
    initialized_ = receiving_ = text_open_ = read_ready_ = false;
    transfer_ = TransferState::Idle;
    read_step_ = ReadStep::Idle;
    init_step_ = InitStep::Preamble;
    started_ = last_frame_ = receive_progress_ = now;
    last_read_ = now - timing::POLL_INTERVAL_MS;
    text_count_ = 0;
    text_.clear();
    stream_.reset();
}

void InternetSession::stop() {
    active_ = false;
    transfer_ = TransferState::Idle;
}

InternetSession::Snapshot InternetSession::snapshot() const {
    return {active_, initialized_, receiving_, transfer_, started_, last_frame_};
}

bool InternetSession::queue_menu(int item, int value, uint32_t now) {
    if (!active_ || !initialized_ || transfer_ != TransferState::Idle || !valid_menu_id(item)) {
        return false;
    }

    write_item_ = item;
    write_value_ = value;
    write_at_ = now;
    transfer_ = TransferState::Pending;
    return true;
}

void InternetSession::feed(const Bytes& bytes, uint32_t now, const Sender& send,
                           const Logger& log) {
    if (!active_) {
        return;
    }

    if ((receiving_ && now - receive_progress_ >= timing::SESSION_TIMEOUT_MS) ||
        (transfer_ != TransferState::Idle && now - write_at_ > timing::TRANSFER_TIMEOUT_MS)) {
        stop();
        log("Transfer timed out; reconnecting");
        return;
    }

    for (const auto& frame : stream_.feed(bytes, now)) {
        last_frame_ = now;
        respond(frame, now, send, log);

        if (!active_) {
            break;
        }
    }
}

InternetSession::ReceiveResult InternetSession::receive(uint16_t opcode, const Bytes& data,
                                                        uint32_t now, const Logger& log) {
    ReceiveResult result;

    if (opcode == protocol::ANNOUNCE && data.size() == 8 &&
        word(&data[4]) == protocol::TEXT_TRANSFER) {
        receiving_ = true;
        receive_progress_ = now;
        text_open_ = false;
        text_count_ = 0;
        text_.clear();
    } else if (opcode == protocol::DATA && data.size() > 2) {
        receive_progress_ = now;
        const auto first = data.begin() + 2;

        if (!text_open_ && first != data.end() && *first == '<') {
            text_open_ = true;
            text_.clear();
            text_count_ = 0;
        }

        if (text_open_) {
            result.acknowledged = true;
            text_count_ += data.size() - 2;
            text_.append(first, data.end());

            if (text_.size() > protocol::MAX_TEXT_BYTES) {
                stop();
                log("Text exceeds size limit");
                return result;
            }

            if (!text_.empty() && text_.back() == '>') {
                text_open_ = false;
                result.complete = true;
                result.initialized = text_.rfind("<INIT|", 0) == 0;

                if (result.initialized) {
                    initialized_ = true;
                }

                read_step_ = ReadStep::Idle;

                if (callbacks_.text) {
                    callbacks_.text(text_, now);
                }
            }
        }
    } else if (opcode == protocol::DATA && data.size() == 2 && receiving_) {
        receiving_ = false;

        if (text_open_) {
            stop();
            log("Premature text transfer end");
        }
    }

    return result;
}

InternetSession::Bytes InternetSession::initialization_message() {
    switch (init_step_) {
    case InitStep::Preamble:
        init_step_ = InitStep::EndPreamble;
        return {3, 1, 0};
    case InitStep::EndPreamble:
        init_step_ = InitStep::Announce;
        return {3, 1};
    case InitStep::Announce:
        if (metadata_.size() < 2) {
            stop();
            return {};
        }

        init_step_ = InitStep::Metadata;
        return announcement(protocol::BINARY_TRANSFER, metadata_.size() - 2);
    case InitStep::Metadata:
        init_step_ = InitStep::EndMetadata;
        return metadata_;
    case InitStep::EndMetadata:
        init_step_ = InitStep::Idle;
        return {3, 1};
    case InitStep::Idle:
        return {3, 2};
    }

    return {3, 2};
}

InternetSession::Bytes InternetSession::read_message(const Bytes& route, uint32_t now) {
    if (read_step_ == ReadStep::Idle && now - last_read_ >= timing::POLL_INTERVAL_MS) {
        last_read_ = now;
        read_ready_ = false;
        read_step_ = ReadStep::AwaitingReady;
        return announcement(protocol::BINARY_TRANSFER, 4);
    }

    if (read_step_ == ReadStep::AwaitingReady && ready(route)) {
        read_ready_ = true;
        read_step_ = ReadStep::AwaitingAcknowledgment;
        return {3, 1, 3, 0x1e, 0, 1};
    }

    if (read_step_ == ReadStep::AwaitingAcknowledgment && read_ready_ && acknowledged(route, 4)) {
        read_ready_ = false;
        read_step_ = ReadStep::Idle;

        if (callbacks_.poll_ack) {
            callbacks_.poll_ack(now);
        }

        return {3, 1};
    }

    // An unanswered read must not leave future requests waiting forever.
    if (read_step_ != ReadStep::Idle && now - last_read_ > timing::TRANSFER_TIMEOUT_MS) {
        stop();
    }

    return {3, 2};
}

InternetSession::Bytes InternetSession::write_message(const Bytes& route, uint32_t now) {
    const std::string text =
        "<X|0.0.1|" + std::to_string(write_item_) + "," + std::to_string(write_value_) + ">";

    if (transfer_ == TransferState::Pending) {
        transfer_ = TransferState::AwaitingReady;
        return announcement(protocol::TEXT_TRANSFER, text.size());
    }

    if (transfer_ == TransferState::AwaitingReady && ready(route)) {
        transfer_ = TransferState::AwaitingAcknowledgment;

        if (callbacks_.sent) {
            callbacks_.sent(write_item_, write_value_, now);
        }

        Bytes data{3, 1};
        data.insert(data.end(), text.begin(), text.end());
        return data;
    }

    if (transfer_ == TransferState::AwaitingAcknowledgment && acknowledged(route, text.size())) {
        transfer_ = TransferState::Idle;
        last_read_ = now - timing::POLL_INTERVAL_MS;

        if (callbacks_.acknowledged) {
            callbacks_.acknowledged(write_item_, write_value_);
        }

        return {3, 1};
    }

    return {3, 2};
}

InternetSession::Bytes InternetSession::next_message(const Bytes& route,
                                                     const ReceiveResult& received, uint32_t now) {
    if (receiving_ || text_open_ || received.complete) {
        return {3, 2};
    }

    if (!initialized_) {
        return initialization_message();
    }

    if (transfer_ != TransferState::Idle &&
        (transfer_ != TransferState::Pending || read_step_ == ReadStep::Idle)) {
        return write_message(route, now);
    }

    return read_message(route, now);
}

void InternetSession::respond(const Frame& frame, uint32_t now, const Sender& send,
                              const Logger& log) {
    if (frame.bootstrap) {
        if (initialized_ || init_step_ != InitStep::Preamble) {
            // The controller restarted without a local UART disconnect.
            start(now);

            if (callbacks_.restarted) {
                callbacks_.restarted();
            }
        }

        send(encode_bootstrap(protocol::INTERNET_ADDRESS, protocol::ANNOUNCE));
        return;
    }

    const Bytes* route = nullptr;
    const Bytes* data = nullptr;

    for (const auto& field : frame.fields) {
        if (field.tag == protocol::MODULE_TAG) {
            data = &field.value;
        } else if (field.tag == protocol::CONTROLLER_TAG) {
            route = &field.value;
        }
    }

    if (!data) {
        stop();
        log("Missing data field");
        return;
    }

    if (!route) {
        send(frame.recovery ? encode(protocol::INTERNET_ADDRESS,
                                     {{protocol::CONTROLLER_TAG, {1, 1, 0, 0, 0, 1, 0, 1}}})
                            : encode_bootstrap(protocol::INTERNET_ADDRESS, protocol::ANNOUNCE));
        return;
    }

    const bool short_route =
        route->size() == 4 && (*route)[0] == 2 && (*route)[2] == 0xff && (*route)[3] == 0xff;
    const bool long_route =
        route->size() == 6 && (*route)[0] == 4 && (*route)[4] == 0xff && (*route)[5] == 0xff;
    // A cold controller can announce its initial transfer before there is anything to ACK.
    const bool cold_start = route->empty() && data->size() == 8 &&
                            word(data->data()) == protocol::ANNOUNCE &&
                            word(&(*data)[4]) == protocol::STARTUP_TRANSFER;

    if (!short_route && !long_route && !cold_start) {
        stop();
        log("Invalid route acknowledgment");
        return;
    }

    if (cold_start && (initialized_ || init_step_ != InitStep::Preamble)) {
        start(now);

        if (callbacks_.restarted) {
            callbacks_.restarted();
        }
    }

    if (data->size() < 2) {
        stop();
        return;
    }

    const auto opcode = word(data->data());

    if (opcode != protocol::ANNOUNCE && opcode != protocol::DATA && opcode != protocol::IDLE) {
        stop();
        log("Unsupported message opcode");
        return;
    }

    const auto received = receive(opcode, *data, now, log);
    Bytes reply_route =
        opcode == protocol::ANNOUNCE ? Bytes{2, 1, 0xff, 0xff} : Bytes{4, 0, 0, 0, 0xff, 0xff};

    if (received.acknowledged) {
        reply_route = {4,
                       uint8_t(text_count_ >> 8),
                       uint8_t(text_count_),
                       uint8_t(received.complete ? 3 : 1),
                       0xff,
                       0xff};
    }

    const auto message = next_message(*route, received, now);

    if (active_) {
        send(encode(protocol::INTERNET_ADDRESS,
                    {{protocol::MODULE_TAG, reply_route}, {protocol::CONTROLLER_TAG, message}}));
    }
}
