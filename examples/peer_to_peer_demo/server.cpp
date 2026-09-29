#include <engine/networking/session.hpp>

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t stopping = 0;

void requestStop(int) {
    stopping = 1;
}

std::uint16_t parsePort(const char* text) {
    const std::string input(text);
    std::size_t end = 0;
    const int value = std::stoi(input, &end);
    if (end != input.size() || value < 1 || value > 65535)
        throw std::invalid_argument("ports must be integers from 1 to 65535");
    return static_cast<std::uint16_t>(value);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 3) {
        std::cerr << "Usage: peer_to_peer_server [registration-port first-control-port]\n";
        return 2;
    }

    try {
        const auto registrationPort = argc == 3 ? parsePort(argv[1]) : std::uint16_t{5555};
        const auto controlBase = argc == 3 ? parsePort(argv[2]) : std::uint16_t{6000};
        if (static_cast<unsigned>(controlBase) + 255 > 65535 ||
            (registrationPort > controlBase && registrationPort <= controlBase + 255))
            throw std::invalid_argument("control ports must fit and not overlap registration");

        // The callback may run on several control threads, so it uses only its argument.
        engine::networking::CoordinatorServer server(
            registrationPort, controlBase, [](double seconds) {
                return engine::networking::PlayerPosition{
                    480.f + 220.f * std::sin(static_cast<float>(seconds) * 0.7f),
                    250.f + 110.f * std::cos(static_cast<float>(seconds) * 0.5f)};
            });

        std::signal(SIGINT, requestStop);
        std::signal(SIGTERM, requestStop);
        server.start();
        std::cout << "Coordinator listening on tcp://*:" << registrationPort
                  << "; control ports start at " << controlBase + 1
                  << ". Press Ctrl+C to stop.\n";
        while (!stopping)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Coordinator error: " << error.what() << '\n';
        return 1;
    }
}
