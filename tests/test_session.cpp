// SPDX-License-Identifier: MIT
#include "support/controller_peer.h"
#include <iostream>
using namespace tech_rs_gateway;

int main() {
    ControllerService service;
    ControllerPeer peer(service);
    peer.initialize();
    const auto last_poll = service.snapshot(peer.now).poll_age_ms;
    peer.idle();
    peer.ack(4);
    assert(*service.snapshot(peer.now).poll_age_ms > *last_poll);
    peer.text("<MU|1.0.0|1|101,0,1,1,759,0,0,30,10,50,30,1035>");
    assert(service.request_write("MU:101", "31", "30", peer.now).accepted());
    service.tick(++peer.now);
    peer.idle();
    peer.ready();
    assert(service.snapshot(peer.now).write_state == WriteState::AwaitingAcknowledgment);
    peer.initialize(false); // Controller reboot, without resetting service internals.
    assert(service.snapshot(peer.now).result.code == ResultCode::Reconnected);
    assert(!service.next_item("", peer.now)->fresh);
    assert(!service.snapshot(peer.now).busy());
    const auto count = peer.transmitted.size();
    peer.idle();
    assert(peer.transmitted.size() == count + 1);
    assert(peer.last_data() == Bytes({3, 2}));
    peer.frame({4, 0, 0, 0, 0xff, 0xff}, {1, 1, 0, 0, 0, 0x49, 0, 100});
    peer.now += timing::SESSION_TIMEOUT_MS;
    peer.idle();
    assert(!service.snapshot(peer.now).online);
    assert(service.reconnect_due(peer.now, 100000));
    std::cout << "PASS: complete initialization, poll gating, reconnect cancellation and stalled "
                 "receive\n";
}
