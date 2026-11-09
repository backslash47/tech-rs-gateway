// SPDX-License-Identifier: MIT
#include "../components/tech_rs_gateway/wire.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace tech_rs_gateway;

int main() {
    const std::string check = "123456789";
    assert(crc16(reinterpret_cast<const uint8_t*>(check.data()), check.size()) == 0x6f91);
    Frame frame;
    Bytes bootstrap{2, 0x26, 0xff, 0xf4, 0x17, 0x88, 0, 0, 2, 0x18, 0x5d, 0x9f};
    assert(decode(bootstrap, frame) && frame.bootstrap && frame.bootstrap_code == 0);
    Bytes reset{2, 0x26, 0xff, 0xf4, 0x17, 0x88, 1, 1, 2, 0x18, 0x55, 0xce};
    assert(decode(reset, frame) && frame.bootstrap && frame.bootstrap_code == 0x0101);
    Bytes ready{2,    0x26, 0xff, 0xf4, 4, 0, 0, 4, 2, 1,    0xff, 0xff,
                0x17, 0x88, 0,    2,    3, 2, 0, 0, 2, 0x18, 0xa9, 0x8f};
    assert(decode(ready, frame) && !frame.bootstrap && frame.fields.size() == 2);
    assert(frame.fields[0].tag == 0x0400 && frame.fields[0].value == Bytes({2, 1, 0xff, 0xff}));
    assert(encode(frame.address, frame.fields) == ready);
    assert(encode(0xfff4, {}).empty());
    assert(encode(0xfff4, {{0x0400, Bytes(65536)}}).empty());
    // Embedded framing markers are data, including when followed by CRC-like bytes.
    auto marker = encode(0xfff4, {{0x1788, {3, 1, 2, 0x18, 2, 0x26, 0xff, 0xff}}});
    assert(decode(marker, frame) && frame.fields[0].value.size() == 8);

    for (size_t i = 0; i < ready.size(); ++i) {
        assert(!decode(Bytes(ready.begin(), ready.begin() + i), frame));
        auto corrupt = ready;
        corrupt[i] ^= 1;
        assert(!decode(corrupt, frame));
    }

    auto bad_length = ready;
    bad_length[7] = 40;
    const auto crc = crc16(bad_length.data(), bad_length.size() - 4);
    bad_length[bad_length.size() - 2] = crc >> 8;
    bad_length.back() = crc;
    assert(!decode(bad_length, frame)); // Valid CRC does not excuse impossible length.
    for (size_t length = 0; length < 257; ++length) {
        const Bytes value(length, 0xa5);
        auto packet = encode(0xfff4, {{0x0400, value}});
        assert(decode(packet, frame) && frame.fields[0].value == value);
    }

    Bytes nested{3, 1};
    nested.insert(nested.end(), ready.begin(), ready.end());
    const auto nested_packet = encode(0xfff4, {{0x1788, nested}});

    for (size_t split = 1; split < nested_packet.size(); ++split) {
        FrameStream stream;
        assert(
            stream.feed(Bytes(nested_packet.begin(), nested_packet.begin() + split), 100).empty());
        const auto result =
            stream.feed(Bytes(nested_packet.begin() + split, nested_packet.end()), 101);
        assert(result.size() == 1 && result[0].fields[0].value == nested);
    }

    FrameStream stream;
    auto corrupt_packet = ready;
    corrupt_packet.back() ^= 1;
    corrupt_packet.insert(corrupt_packet.end(), ready.begin(), ready.end());
    assert(stream.feed(corrupt_packet, 200).size() == 1);
    Bytes combined = ready;
    combined.insert(combined.end(), bootstrap.begin(), bootstrap.end());
    assert(stream.feed(combined, 201).size() == 2);
    stream.reset();
    assert(stream.feed(Bytes(ready.begin(), ready.begin() + 10), 1000).empty());
    assert(stream.feed(ready, 1400).size() == 1);
    Bytes recovery{2, 0x26, 0xff, 0xf4, 4, 0, 0, 6, 0x17, 0x88, 0, 2, 3, 2, 0, 0};
    const auto recovery_crc = crc16(recovery.data(), recovery.size());
    append_word(recovery, 0x0218);
    append_word(recovery, recovery_crc);
    const auto recovered = stream.feed(recovery, 1401);
    assert(recovered.size() == 1 && recovered[0].recovery);
    assert(recovered[0].fields.size() == 1);

    std::cout
        << "PASS: wire vectors, CRC, padding, embedded markers, truncation and length validation\n";
}
