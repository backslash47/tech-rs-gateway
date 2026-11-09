// SPDX-License-Identifier: MIT
#include "../components/tech_rs_gateway/compressed_labels.h"
#include "../components/tech_rs_gateway/dynamic_menu_assets.h"
#include "../components/tech_rs_gateway/label_dictionary_data.h"
#include <cassert>
#include <iostream>
#include <string>
#include <zlib.h>

using namespace tech_rs_gateway;
size_t inflations = 0;

bool inflate_member(const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) {
    ++inflations;
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(input);
    stream.avail_in = input_size;
    stream.next_out = output;
    stream.avail_out = output_size;
    assert(inflateInit2(&stream, 16 + MAX_WBITS) == Z_OK);
    const int result = inflate(&stream, Z_FINISH);
    const bool valid =
        result == Z_STREAM_END && stream.total_in == input_size && stream.total_out == output_size;
    inflateEnd(&stream);
    return valid;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string language = argv[1];
    LabelDictionary dictionary{};

    if (language == "cs") {
        dictionary = {menu_labels_cs_gz, sizeof(menu_labels_cs_gz), label_blocks_cs,
                      sizeof(label_blocks_cs) / sizeof(label_blocks_cs[0])};
    } else if (language == "pl") {
        dictionary = {menu_labels_pl_gz, sizeof(menu_labels_pl_gz), label_blocks_pl,
                      sizeof(label_blocks_pl) / sizeof(label_blocks_pl[0])};
    } else {
        assert(language == "en");
        dictionary = {menu_labels_en_gz, sizeof(menu_labels_en_gz), label_blocks_en,
                      sizeof(label_blocks_en) / sizeof(label_blocks_en[0])};
    }

    CompressedLabels reader(dictionary, inflate_member);
    uint32_t code;

    while (std::cin >> code) {
        const auto literal = reader.find(code);
        std::cout << (literal.empty() ? "null" : literal) << '\n';
    }

    // Ordered requests should inflate each block at most once, not once per label.
    assert(inflations <= dictionary.block_count);
    assert(reader.find(UINT32_MAX).empty());
    const auto first = dictionary.blocks[0].first_code;
    CompressedLabels broken(dictionary, [](const uint8_t*, size_t, uint8_t*, size_t) {
        return false;
    });
    assert(broken.find(first).empty());
    assert(!reader.find(first).empty());
}
