// SPDX-License-Identifier: MIT
#include "compressed_labels.h"

namespace tech_rs_gateway {

CompressedLabels::CompressedLabels(const LabelDictionary& dictionary, InflateLabelBlock inflate)
    : dictionary_(dictionary), inflate_(inflate) {
}

std::string_view CompressedLabels::find(uint32_t code) {
    size_t low = 0;
    size_t high = dictionary_.block_count;

    while (low < high) {
        const size_t middle = low + (high - low) / 2;

        if (dictionary_.blocks[middle].first_code <= code) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    if (low == 0 || !load(low - 1)) {
        return {};
    }

    const std::string_view json(reinterpret_cast<const char*>(buffer_.get()), length_);
    size_t cursor = 0;

    while (cursor < json.size()) {
        while (cursor < json.size() && (json[cursor] == '{' || json[cursor] == ',')) {
            ++cursor;
        }

        if (cursor == json.size() || json[cursor] == '}') {
            return {};
        }

        const auto key = string_token(json, cursor);

        if (key.size() < 3 || cursor >= json.size() || json[cursor++] != ':') {
            return {};
        }

        uint32_t parsed_code = 0;

        for (size_t index = 1; index + 1 < key.size(); ++index) {
            const char digit = key[index];

            if (digit < '0' || digit > '9' ||
                parsed_code > (UINT32_MAX - static_cast<unsigned>(digit - '0')) / 10) {
                return {};
            }

            parsed_code = parsed_code * 10 + static_cast<unsigned>(digit - '0');
        }

        const auto value = string_token(json, cursor);

        if (value.empty()) {
            return {};
        }

        if (parsed_code == code) {
            return value;
        }

        if (parsed_code > code) {
            return {};
        }
    }

    return {};
}

bool CompressedLabels::load(size_t index) {
    if (cached_block_ == index) {
        return true;
    }

    cached_block_ = SIZE_MAX;
    length_ = 0;
    const auto& block = dictionary_.blocks[index];

    if (block.unpacked_size > LABEL_BLOCK_SIZE || block.offset > dictionary_.size ||
        block.compressed_size > dictionary_.size - block.offset) {
        return false;
    }

    if (!buffer_) {
        buffer_.reset(new (std::nothrow) uint8_t[LABEL_BLOCK_SIZE]);
    }

    if (!buffer_ || !inflate_(dictionary_.data + block.offset, block.compressed_size, buffer_.get(),
                              block.unpacked_size)) {
        return false;
    }

    cached_block_ = index;
    length_ = block.unpacked_size;
    return true;
}

std::string_view CompressedLabels::string_token(std::string_view json, size_t& cursor) {
    const size_t start = cursor;

    if (cursor >= json.size() || json[cursor++] != '"') {
        return {};
    }

    while (cursor < json.size()) {
        const char character = json[cursor++];

        if (character == '"') {
            return json.substr(start, cursor - start);
        }

        if (character == '\\') {
            if (cursor >= json.size()) {
                return {};
            }

            ++cursor;
        }
    }

    return {};
}

} // namespace tech_rs_gateway
