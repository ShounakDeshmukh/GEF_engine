#include <chrono>
#include <engine/networking/session.hpp>
#include <engine/timeline.hpp>
#include <iostream>
#include <set>
#include <string>
#include <thread>

namespace {
engine::networking::Bytes number(std::int64_t value) {
    engine::networking::Bytes result;
    for (unsigned i = 0; i < 8; ++i)
        result.push_back(std::byte((value >> (i * 8)) & 255));
    return result;
}
} // namespace
int main(int argc, char** argv) {
    if (argc < 4)
        return 2;
    try {
        const auto registration = static_cast<std::uint16_t>(std::stoi(argv[2]));
        if (std::string(argv[1]) == "server") {
            engine::networking::Server server({.joinPort = registration, .relayPlayers = false});
            server.start();
            engine::Timeline realTime;
            engine::Timeline worldTime(realTime, 60);
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (std::chrono::steady_clock::now() < until) {
                const auto tick = worldTime.now();
                server.publishWorld(number(tick), tick);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return 0;
        }
        if (argc != 7)
            return 2;
        const auto port = static_cast<std::uint16_t>(std::stoi(argv[4]));
        const int rate = std::stoi(argv[5]);
        const int millis = std::stoi(argv[6]);
        engine::networking::Client peer({.serverHost = "127.0.0.1",
                                         .joinPort = registration,
                                         .heartbeat = std::chrono::milliseconds(50),
                                         .peerToPeer = true,
                                         .peerPort = port});
        peer.start();
        const auto self = peer.id();
        std::set<engine::ClientId> seen, left;
        bool worldReceived = false, sharedClockConsistent = true;
        std::int64_t firstTick = -1, lastTick = 0, playerTick = 0;
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(millis);
        auto nextSend = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() < until) {
            for (const auto& event : peer.drain()) {
                using Kind = engine::networking::ServerEvent::Kind;
                if (event.kind == Kind::Player)
                    seen.insert(event.client);
                else if (event.kind == Kind::PlayerLeft)
                    left.insert(event.client);
                else if (!event.payload.empty()) {
                    worldReceived = true;
                    sharedClockConsistent &= event.payload == number(event.tick);
                    if (firstTick < 0)
                        firstTick = event.tick;
                    lastTick = event.tick;
                }
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextSend) {
                peer.send(number(self), ++playerTick);
                nextSend = now + std::chrono::milliseconds(1000 / rate);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::cout << "self=" << self << " world=" << worldReceived
                  << " moving=" << (lastTick - firstTick > 60)
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
