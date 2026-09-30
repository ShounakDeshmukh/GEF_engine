#include <engine/engine.hpp>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
volatile std::sig_atomic_t stopping = 0;
void requestStop(int) { stopping = 1; }
} // namespace

int main(int argc, char** argv) {
    try {
        engine::networking::ServerConfig config;
        config.relayPlayers = false; // The server is only a peer directory.
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) != "--port" || i + 1 >= argc)
                throw std::invalid_argument("Usage: peer_to_peer_server [--port 1..65535]");
            const int port = std::stoi(argv[++i]);
            if (port < 1 || port > 65535)
                throw std::invalid_argument("port must be 1..65535");
            config.joinPort = static_cast<std::uint16_t>(port);
        }

        engine::networking::Server server(config);
        server.start();
        std::signal(SIGINT, requestStop);
        std::signal(SIGTERM, requestStop);
        std::cout << "Peer directory listening on port " << server.port() << '\n';

        std::size_t previousCount = 0;
        while (!stopping) {
            const auto count = server.connectedClients();
            if (count != previousCount) {
                std::cout << "Connected peers: " << count << '\n';
                previousCount = count;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        server.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Server error: " << error.what() << '\n';
        return 1;
    }
}
