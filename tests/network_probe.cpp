#include <chrono>
#include <engine/networking/session.hpp>
#include <iostream>
#include <set>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc < 2)
        return 2;
    try {
        const int registration = std::stoi(argv[2]);
        const int control = std::stoi(argv[3]);
        if (std::string(argv[1]) == "server") {
            engine::networking::CoordinatorServer server(
                static_cast<std::uint16_t>(registration), static_cast<std::uint16_t>(control),
                [](double seconds) {
                    return engine::networking::PlayerPosition{
                        1100.f + static_cast<float>(seconds * 10), 440.f};
                });
            server.start();
            std::this_thread::sleep_for(std::chrono::seconds(8));
            return 0;
        }
        if (argc != 7)
            return 2;
        const int port = std::stoi(argv[4]);
        const int rate = std::stoi(argv[5]);
        const int millis = std::stoi(argv[6]);
        engine::networking::PeerClient peer("tcp://127.0.0.1:" + std::to_string(registration),
                                            "tcp://127.0.0.1:" + std::to_string(port), rate);
        peer.start();
        std::set<engine::ClientId> seen;
        std::set<engine::ClientId> left;
        engine::ClientId self = 0;
        bool worldReceived = false;
        float firstWorldX = -1.f;
        float lastWorldX = -1.f;
        bool sharedClockConsistent = true;
        std::set<engine::ClientId> previous;
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis);
        while (std::chrono::steady_clock::now() < until) {
            for (const auto& event : peer.drain()) {
                if (event.kind == engine::networking::PeerEvent::Kind::Welcome)
                    self = event.sender;
                else if (event.kind == engine::networking::PeerEvent::Kind::Player)
                    seen.insert(event.sender);
                else {
                    worldReceived = event.world.droneX >= 1100.f && event.world.droneY == 440.f;
                    if (firstWorldX < 0.f)
                        firstWorldX = event.world.droneX;
                    lastWorldX = event.world.droneX;
                    const float originEstimate =
                        event.world.droneX - static_cast<float>(event.world.tick) / 60.f * 10.f;
                    if (originEstimate < 1099.5f || originEstimate > 1100.5f)
                        sharedClockConsistent = false;
                    std::set<engine::ClientId> current;
                    for (const auto& info : event.world.peers)
                        current.insert(info.id);
                    for (auto id : previous)
                        if (!current.contains(id))
                            left.insert(id);
                    previous = std::move(current);
                }
            }
            peer.publish({static_cast<float>(self * 100), static_cast<float>(self * 10)});
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::cout << "self=" << self << " world=" << worldReceived
                  << " moving=" << (lastWorldX - firstWorldX > 10.f)
                  << " sharedClock=" << sharedClockConsistent << " seen=";
        for (auto id : seen)
            std::cout << id << ',';
        std::cout << " left=";
        for (auto id : left)
            std::cout << id << ',';
        std::cout << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
