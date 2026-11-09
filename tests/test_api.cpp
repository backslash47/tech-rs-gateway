// SPDX-License-Identifier: MIT
#include "../components/tech_rs_gateway/menu_api.h"
#include "support/controller_peer.h"
#include <future>
#include <iostream>
using namespace tech_rs_gateway;

int main() {
    ControllerService service;
    ControllerPeer peer(service);
    peer.initialize();
    peer.text("<MU|1.0.0|1|101,0,1,1,759,0,0,30,10,50,30,1035>");
    assert(!MenuApi::authorized("bad", "http://gateway", "gateway", "secret"));
    assert(!MenuApi::authorized("secret", "http://other", "gateway", "secret"));
    assert(MenuApi::authorized("secret", "http://gateway", "gateway", "secret"));
    assert(MenuApi::write(service, false, "MU:101", "31", "30", false, peer.now).status == 403);
    assert(MenuApi::write(service, true, "MU:101", std::nullopt, "30", false, peer.now).status ==
           400);
    assert(MenuApi::write(service, true, "MU:101", "31", "29", false, peer.now).status == 409);
    assert(MenuApi::menu(service, "MU", "-1", peer.now, "secret", 100000).status == 400);
    assert(MenuApi::menu(service, "MU", "0", peer.now, "secret", 100000).status == 200);
    // A delayed/error HTTP response can be retained without holding the model lock.
    const auto error = MenuApi::write(service, true, "MU:101", "99", "30", false, peer.now);
    auto task = std::async(std::launch::async, [&] {
        for (int n = 0; n < 100; ++n) {
            service.snapshot(peer.now);
        }
    });
    assert(task.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    task.get();
    assert(error.body.find("out_of_range") != std::string::npos);
    assert(MenuApi::write(service, true, "MU:101", "31", "30", false, peer.now).status == 202);
    assert(peer.dispatch_write() == "<X|0.0.1|101,31>");
    peer.text("<MU|1.0.0|0|101,0,1,1,759,0,0,31,10,50,30,1035>");
    // Competing adapter threads cannot stage two commands at once.
    auto first = std::async(std::launch::async, [&] {
        return service.request_write("MU:101", "32", "31", peer.now).accepted();
    });
    auto second = std::async(std::launch::async, [&] {
        return service.request_write("MU:101", "33", std::nullopt, peer.now).accepted();
    });
    assert(int(first.get()) + int(second.get()) == 1);
    std::cout << "PASS: HTTP authorization, validation, snapshots and unlocked response lifetime\n";
}
