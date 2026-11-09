// SPDX-License-Identifier: MIT
#include "wire.h"
#include "protocol_constants.h"
#include <algorithm>

namespace tech_rs_gateway {

uint16_t crc16(const uint8_t* data, size_t length) {
    uint16_t remainder = 0xffff;

    for (size_t position = 0; position < length; ++position) {
        remainder ^= data[position];

        for (unsigned bit = 0; bit < 8; ++bit) {
            const bool low = (remainder & 1) != 0;
            remainder >>= 1;

            if (low) {
                remainder ^= 0x8408;
            }
        }
    }

    return remainder;
}

bool decode(const Bytes& packet, Frame& result) {
    if (packet.size() < 12 || packet.size() % 4 || packet[0] != 2 || packet[1] != 0x26) {
        return false;
    }

    const size_t end = packet.size() - 4;

    if (word(&packet[end]) != 0x0218 || crc16(packet.data(), end) != word(&packet[end + 2])) {
        return false;
    }

    Frame parsed;
    parsed.address = word(&packet[2]);

    if (end == 8 && word(&packet[4]) == 0x1788 &&
        (word(&packet[6]) == 0 || word(&packet[6]) == 0x0101)) {
        parsed.bootstrap = true;
        parsed.bootstrap_code = word(&packet[6]);
    } else {
        size_t offset = 4;

        while (offset < end) {
            if (end - offset < 4) {
                return false;
            }

            const size_t length = word(&packet[offset + 2]);
            // The recovery form omits the declared route bytes entirely.
            if (word(&packet[offset]) == protocol::CONTROLLER_TAG && (length == 4 || length == 6) &&
                offset + 8 <= end && word(&packet[offset + 4]) == protocol::MODULE_TAG) {
                parsed.recovery = true;
                offset += 4;
                continue;
            }

            const size_t padded = (length + 3) & ~size_t(3);

            if (padded > end - offset - 4) {
                return false;
            }

            parsed.fields.push_back(
                {word(&packet[offset]),
                 Bytes(packet.begin() + offset + 4, packet.begin() + offset + 4 + length)});
            offset += 4 + padded;
        }

        if (parsed.fields.empty()) {
            return false;
        }
    }

    result = std::move(parsed);
    return true;
}

Bytes encode(uint16_t address, const std::vector<Tlv>& fields) {
    // Empty result signals invalid input; compatible with ESPHome's no-exceptions build.
    if (fields.empty()) {
        return {};
    }

    Bytes packet{2, 0x26};
    append_word(packet, address);

    for (const auto& field : fields) {
        if (field.value.size() > 65535) {
            return {};
        }

        append_word(packet, field.tag);
        append_word(packet, uint16_t(field.value.size()));
        packet.insert(packet.end(), field.value.begin(), field.value.end());

        while (packet.size() % 4) {
            packet.push_back(0);
        }
    }

    const uint16_t checksum = crc16(packet.data(), packet.size());
    append_word(packet, 0x0218);
    append_word(packet, checksum);
    return packet;
}

Bytes encode_bootstrap(uint16_t address, uint16_t code) {
    Bytes packet{2, 0x26};
    append_word(packet, address);
    append_word(packet, protocol::MODULE_TAG);
    append_word(packet, code);
    const auto checksum = crc16(packet.data(), packet.size());
    append_word(packet, 0x0218);
    append_word(packet, checksum);
    return packet;
}

void FrameStream::reset() {
    buffer_.clear();
    last_rx_ = 0;
}

size_t FrameStream::frame_size() const {
    if (buffer_.size() < 8) {
        return 0;
    }

    if (word(&buffer_[4]) == protocol::MODULE_TAG &&
        (word(&buffer_[6]) == 0 || word(&buffer_[6]) == protocol::ANNOUNCE)) {
        return buffer_.size() >= 12 ? 12 : 0;
    }

    size_t offset = 4;

    while (offset + 4 <= buffer_.size()) {
        const auto tag = word(&buffer_[offset]);

        if (tag == 0x0218) {
            return offset + 4;
        }

        if (tag != protocol::CONTROLLER_TAG && tag != protocol::MODULE_TAG) {
            return SIZE_MAX;
        }

        const size_t length = word(&buffer_[offset + 2]);

        if (tag == protocol::CONTROLLER_TAG && (length == 4 || length == 6)) {
            if (offset + 6 > buffer_.size()) {
                return 0;
            }

            if (word(&buffer_[offset + 4]) == protocol::MODULE_TAG) {
                offset += 4;
                continue;
            }
        }

        const size_t next = offset + 4 + ((length + 3) & ~size_t(3));

        if (next + 4 > protocol::MAX_FRAME_BYTES) {
            return SIZE_MAX;
        }

        if (next > buffer_.size()) {
            return 0;
        }

        offset = next;
    }

    return 0;
}

void FrameStream::drain(std::vector<Frame>& output) {
    static const Bytes header{2, 0x26, 0xff, 0xf4};

    for (;;) {
        auto found = std::search(buffer_.begin(), buffer_.end(), header.begin(), header.end());

        if (found == buffer_.end()) {
            if (buffer_.size() > header.size() - 1) {
                buffer_.erase(buffer_.begin(), buffer_.end() - (header.size() - 1));
            }

            return;
        }

        buffer_.erase(buffer_.begin(), found);
        const auto size = frame_size();

        if (size == 0) {
            return;
        }

        Frame frame;

        if (size != SIZE_MAX && decode(Bytes(buffer_.begin(), buffer_.begin() + size), frame)) {
            output.push_back(std::move(frame));
            buffer_.erase(buffer_.begin(), buffer_.begin() + size);
        } else {
            buffer_.erase(buffer_.begin());
        }
    }
}

std::vector<Frame> FrameStream::feed(const Bytes& incoming, uint32_t now) {
    if (now - last_rx_ > timing::FRAME_GAP_MS) {
        buffer_.clear();
    }

    last_rx_ = now;
    std::vector<Frame> frames;
    size_t position = 0;

    while (position < incoming.size()) {
        if (buffer_.size() >= protocol::MAX_FRAME_BYTES) {
            buffer_.clear();
        }

        const auto count =
            std::min(protocol::MAX_FRAME_BYTES - buffer_.size(), incoming.size() - position);
        buffer_.insert(buffer_.end(), incoming.begin() + position,
                       incoming.begin() + position + count);
        position += count;
        drain(frames);
    }

    return frames;
}
} // namespace tech_rs_gateway
