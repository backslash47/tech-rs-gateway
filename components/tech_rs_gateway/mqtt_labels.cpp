// SPDX-License-Identifier: MIT
#include "mqtt_labels.h"
#include "compressed_labels.h"
#include "esphome/components/json/json_util.h"
#include "label_dictionary_data.h"
#include "miniz.h"
#include <memory>
#include <new>

namespace {

bool inflate_label_block(const uint8_t* input, size_t input_size, uint8_t* output,
                         size_t output_size) {
    // The generator emits standard gzip members without optional headers.
    if (input_size < 18 || input[0] != 0x1f || input[1] != 0x8b || input[2] != 8 || input[3] != 0) {
        return false;
    }

    // ROM miniz's state is too large for the ESPHome loop task's stack.
    std::unique_ptr<tinfl_decompressor> state(new (std::nothrow) tinfl_decompressor);

    if (!state) {
        return false;
    }

    tinfl_init(state.get());
    size_t compressed_length = input_size - 18;
    size_t decoded_length = output_size;
    const auto result =
        tinfl_decompress(state.get(), input + 10, &compressed_length, output, output,
                         &decoded_length, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    const auto little_endian = [](const uint8_t* bytes) {
        return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 |
               uint32_t(bytes[3]) << 24;
    };
    return result == TINFL_STATUS_DONE && decoded_length == output_size &&
           compressed_length == input_size - 18 &&
           little_endian(input + input_size - 4) == output_size &&
           little_endian(input + input_size - 8) == mz_crc32(MZ_CRC32_INIT, output, output_size);
}

} // namespace

std::string mqtt_menu_label(int code) {
    static tech_rs_gateway::CompressedLabels labels(mqtt_dictionary, inflate_label_block);

    if (code >= 0) {
        const auto literal = labels.find(static_cast<uint32_t>(code));

        if (!literal.empty()) {
            JsonDocument decoded;
            const auto error = deserializeJson(decoded, literal.data(), literal.size());

            if (!error && decoded.is<std::string>()) {
                return decoded.as<std::string>();
            }
        }
    }

    return "TECH " + std::to_string(code);
}
