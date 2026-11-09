// SPDX-License-Identifier: MIT
#include "support/controller_peer.h"
#include <iostream>
using namespace tech_rs_gateway;

int main() {
    int parsed = 0;
    assert(DynamicMenu::integer("2147483647", parsed) && parsed == INT_MAX);
    assert(DynamicMenu::integer("-2147483648", parsed) && parsed == INT_MIN);

    for (const auto* input :
         {"2147483648", "-2147483649", "9999999999", "-9999999999", "", "-", "1x"}) {
        parsed = 123;
        assert(!DynamicMenu::integer(input, parsed) && parsed == 123);
    }

    const std::string menu = "<MU|1.0.0|1|0,0,0,1,388,0,0|101,0,1,1,759,0,0,30,10,50,30,1035|102,0,"
                             "20,1,312,0,0,319,1,-1,0>";
    ControllerService service;
    ControllerPeer peer(service);
    peer.initialize();
    peer.text(menu);
    auto page = service.page("MU", 0, peer.now);
    assert(page.total == 3 && !page.status.overflow);
    assert(!page.rows.back().actionable);
    assert(service.request_write("MU:102", "1", page.rows.back().raw, peer.now, true).code ==
           ResultCode::NotWritable);
    assert(service.request_write("MU:101", "99", "30", peer.now).code == ResultCode::OutOfRange);
    assert(service.request_write("MU:101", "31", "29", peer.now).code == ResultCode::Conflict);
    assert(service.request_write("MU:101", "31", "30", peer.now).accepted());
    assert(peer.dispatch_write() == "<X|0.0.1|101,31>");
    assert(service.snapshot(peer.now).write_state == WriteState::AwaitingReadback);
    peer.idle();
    peer.ready();
    peer.ack(4);
    assert(service.snapshot(peer.now).busy());
    peer.text("<MU|1.0.0|0|101,0,1,1,759,0,0,31,10,50,30,1035>");
    assert(service.snapshot(peer.now).result.code == ResultCode::Confirmed);
    assert(!service.snapshot(peer.now).busy());
    service.set_experimental_actions(true);
    page = service.page("MU", 0, peer.now);
    assert(page.rows.back().actionable);
    assert(service.request_write("MU:102", "1", page.rows.back().raw, peer.now, true).accepted());
    peer.dispatch_write();
    assert(service.snapshot(peer.now).result.code == ResultCode::ActionDelivered);

    peer.text("<MU|1.0.0|0|101,0,1,1,759,0,0,0,-10,10,0,1035>");
    assert(service.request_write("MU:101", "-1", "0", peer.now).accepted());
    peer.dispatch_write();
    peer.text("<MU|1.0.0|0|101,0,1,1,759,0,0,invalid,-10,10,0,1035>");
    assert(service.snapshot(peer.now).write_state == WriteState::AwaitingReadback);
    peer.text("<MU|1.0.0|0|101,0,1,1,759,0,0,-1,-10,10,0,1035>");
    assert(service.snapshot(peer.now).result.code == ResultCode::Confirmed);

    peer.now += 7200000;
    assert(!service.next_item("MU:0", peer.now)->fresh);
    peer.idle();
    peer.ready();
    peer.ack(4);
    assert(service.next_item("MU:0", peer.now)->fresh);
    service.restart(++peer.now);
    assert(!service.next_item("MU:0", peer.now)->fresh);

    peer.initialize();
    peer.text("<MU|1.0.0|1|70000,0,1,1,759,0,0,30,10,50,30,1035|70001,0,20,1,312,0,0,319,1,-1,0>");
    service.set_experimental_actions(true);
    const auto unsupported = service.page("MU", 0, peer.now);
    assert(!unsupported.rows[0].writable && !unsupported.rows[1].actionable);
    assert(service.request_write("MU:70000", "31", "30", peer.now).code == ResultCode::NotWritable);
    assert(service.request_write("MU:70001", "1", unsupported.rows[1].raw, peer.now, true).code ==
           ResultCode::NotWritable);
    assert(!service.snapshot(peer.now).busy());
    peer.text("<MU|1.0.0|1|65535,0,1,1,759,0,0,30,10,50,30,1035>");
    assert(service.request_write("MU:65535", "31", "30", peer.now).accepted());
    assert(peer.dispatch_write() == "<X|0.0.1|65535,31>");

    size_t heap = 47000;
    ControllerService limited([&] {
        return heap;
    });
    ControllerPeer limited_peer(limited);
    limited_peer.initialize();
    limited_peer.text(menu);
    assert(limited.snapshot(limited_peer.now).missing_items == 3);
    heap = 100000;
    limited_peer.text(menu);
    assert(!limited.snapshot(limited_peer.now).overflow);
    assert(limited.page("MU", 0, limited_peer.now).total == 3);
    std::cout << "PASS: public service validation, write states, fresh readback, actions and "
                 "memory recovery\n";
}
