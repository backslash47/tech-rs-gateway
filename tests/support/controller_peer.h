// SPDX-License-Identifier: MIT
#pragma once
#include "../../components/tech_rs_gateway/controller_service.h"
#include <cassert>

/** Synthetic controller peer exercising real public service/session entry points. */
struct ControllerPeer {
    using Bytes = tech_rs_gateway::Bytes;
    ControllerService& service;
    uint32_t now = 1000;
    std::vector<Bytes> transmitted;

    explicit ControllerPeer(ControllerService& service) : service(service) {
    }

    void bytes(const Bytes& data) {
        now += 10;
        service.receive(
            data, now,
            [&](const Bytes& packet) {
                tech_rs_gateway::Frame decoded;
                assert(tech_rs_gateway::decode(packet, decoded));
                transmitted.push_back(packet);
            },
            [](const char*) {
            });
        service.tick(now);
    }

    void frame(const Bytes& route, const Bytes& data) {
        bytes(tech_rs_gateway::encode(0xfff4, {{0x0400, route}, {0x1788, data}}));
    }

    void idle() {
        frame({4, 0, 0, 0, 0xff, 0xff}, {3, 2});
    }

    void ready() {
        frame({2, 1, 0xff, 0xff}, {3, 2});
    }

    void ack(size_t length) {
        frame({4, uint8_t(length >> 8), uint8_t(length), 3, 0xff, 0xff}, {3, 2});
    }

    Bytes last_data() const {
        assert(!transmitted.empty());
        tech_rs_gateway::Frame frame;
        assert(tech_rs_gateway::decode(transmitted.back(), frame));

        for (const auto& field : frame.fields) {
            if (field.tag == 0x0400) {
                return field.value;
            }
        }

        return {};
    }

    void text(const std::string& text) {
        frame({4, 0, 0, 0, 0xff, 0xff},
              {1, 1, 0, 0, 0, 0x49, uint8_t(text.size() >> 8), uint8_t(text.size())});
        Bytes payload{3, 1};
        payload.insert(payload.end(), text.begin(), text.end());
        frame({4, 0, 0, 0, 0xff, 0xff}, payload);
        frame({4, 0, 0, 0, 0xff, 0xff}, {3, 1});
    }

    void initialize(bool restart_service = true, bool cold_start = false) {
        service.configure_network({2, 0, 0, 0, 0, 1}, {192, 0, 2, 10}, {255, 255, 255, 0},
                                  {192, 0, 2, 1}, {192, 0, 2, 1});

        if (restart_service) {
            service.restart(now);
        }

        if (cold_start) {
            // Captured startup announcement with an explicitly empty acknowledgment.
            const Bytes announcement{0x02, 0x26, 0xff, 0xf4, 0x04, 0x00, 0x00, 0x00,
                                     0x17, 0x88, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00,
                                     0x00, 0x01, 0x02, 0x00, 0x02, 0x18, 0xdc, 0xd1};
            const auto count = transmitted.size();

            for (const auto byte : announcement) {
                bytes({byte});
            }

            assert(transmitted.size() == count + 1);
            tech_rs_gateway::Frame reply;
            assert(tech_rs_gateway::decode(transmitted.back(), reply));
            assert(reply.fields[0].tag == 0x1788);
            assert(reply.fields[0].value == Bytes({2, 1, 0xff, 0xff}));
        } else {
            // Fragmented bootstrap, without assigning any private session state.
            for (const auto byte : tech_rs_gateway::encode_bootstrap(0xfff4, 0)) {
                bytes({byte});
            }

            assert(transmitted.back() == tech_rs_gateway::encode_bootstrap(0xfff4, 0x0101));
            idle();
        }

        assert(last_data() == Bytes({3, 1, 0}));
        idle();
        assert(last_data() == Bytes({3, 1}));
        idle();
        const auto announce = last_data();
        assert(announce.size() == 8 && tech_rs_gateway::word(&announce[4]) == 0x50);
        const auto size = tech_rs_gateway::word(&announce[6]);
        ready();
        assert(last_data().size() == size_t(size + 2));
        // Fixed wire-format vector for the synthetic network configured above.
        const Bytes expected_metadata{
            3,   1,    4,   0x15, 0, 0x13, 0x12, 10,  0x14, 0,   2,    0,   0,    0, 0, 1,
            0,   0,    6,   0x28, 0, 6,    0,    0,   0,    6,   0x2a, 0,   0xf9, 1, 0, 0,
            3,   0x1e, 0,   1,    6, 0x2b, 0,    0,   0,    0,   1,    3,   1,    0, 1, 6,
            2,   0,    192, 0,    2, 10,   6,    3,   0,    255, 255,  255, 0,    6, 4, 0,
            192, 0,    2,   1,    6, 5,    0,    192, 0,    2,   1,    3,   0x0a, 0, 1};
        assert(last_data() == expected_metadata);
        ack(size);
        assert(last_data() == Bytes({3, 1}));
        text("<INIT|6|0|>");
        assert(service.snapshot(now).online);
        assert(last_data() == Bytes({1, 1, 0, 0, 0, 0x50, 0, 4}));
        ready();
        assert(last_data() == Bytes({3, 1, 3, 0x1e, 0, 1}));
        ack(4);
        assert(last_data() == Bytes({3, 1}));
        assert(service.snapshot(now).poll_age_ms.has_value());
    }

    std::string dispatch_write() {
        const Bytes read_announcement{1, 1, 0, 0, 0, 0x50, 0, 4};

        if (last_data() == read_announcement) {
            ready();
            ack(4);
        }

        service.tick(++now);
        idle();

        if (last_data() == read_announcement) {
            ready();
            ack(4);
            idle();
        }

        const auto announce = last_data();
        assert(announce.size() == 8 && tech_rs_gateway::word(&announce[4]) == 0x49);
        const auto size = tech_rs_gateway::word(&announce[6]);
        ready();
        auto data = last_data();
        assert(data.size() == size_t(size + 2));
        std::string command(data.begin() + 2, data.end());
        ack(size);
        assert(last_data() == Bytes({3, 1}));
        return command;
    }
};
