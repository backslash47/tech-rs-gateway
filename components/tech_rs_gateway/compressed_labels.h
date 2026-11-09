// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string_view>

namespace tech_rs_gateway {

constexpr size_t LABEL_BLOCK_SIZE = 16384;

/// Index entry for one independently compressed gzip member.
struct LabelBlock {
    uint32_t first_code;
    uint32_t offset;          ///< Byte offset into LabelDictionary::data.
    uint16_t compressed_size; ///< Complete gzip member length in bytes.
    uint16_t unpacked_size;   ///< Decoded fragment length in bytes, at most LABEL_BLOCK_SIZE.
};

/// Non-owning view of immutable compressed data and its code-sorted block index.
struct LabelDictionary {
    const uint8_t* data;
    size_t size;
    const LabelBlock* blocks;
    size_t block_count;
};

/**
 * @brief Inflates one gzip member into bounded caller-owned storage.
 * Arguments are input pointer/byte length, then output pointer/exact byte length.
 * Return true only after validating and producing the entire expected fragment.
 */
using InflateLabelBlock = bool (*)(const uint8_t*, size_t, uint8_t*, size_t);

/** @brief Looks up label literals with a single decoded-block cache.
 * @note Not thread-safe; serialize access and do not share views across lookups.
 */
class CompressedLabels {
  public:
    /**
     * @param dictionary Immutable descriptor and backing arrays that outlive this cache.
     * @param inflate Decompressor callback; storage allocation failures are handled by lookup.
     */
    explicit CompressedLabels(const LabelDictionary& dictionary, InflateLabelBlock inflate);

    /**
     * @brief Finds a JSON string literal, including quotes and escape sequences.
     * @param code Numeric description code, not a controller menu item ID.
     * @return Literal view, or an empty view for missing data/allocation/inflate failure.
     * @note Consume or copy the view before another lookup; loading another block
     * invalidates it. JSON decoding belongs to the caller. An empty label is
     * represented by the nonempty literal "\"\"", not a missing view.
     */
    std::string_view find(uint32_t code);

  private:
    const LabelDictionary& dictionary_;
    InflateLabelBlock inflate_;
    std::unique_ptr<uint8_t[]> buffer_;
    size_t cached_block_ = SIZE_MAX;
    size_t length_ = 0;

    bool load(size_t index);

    static std::string_view string_token(std::string_view json, size_t& cursor);
};

} // namespace tech_rs_gateway
