#include <engine/engine.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

constexpr float windowWidth = 960.f;
constexpr float windowHeight = 540.f;
constexpr float playerSize = 32.f;
constexpr float playerSpeed = 250.f;

engine::Color peerColor(engine::ClientId id) {
    if (id == 0)
        return {255, 255, 255, 255};
    constexpr engine::Color colors[] = {
        {80, 190, 255, 255}, {255, 115, 130, 255}, {140, 225, 140, 255},
        {200, 150, 255, 255}, {255, 175, 75, 255},  {90, 220, 205, 255},
    };
    return colors[(id - 1) % (sizeof(colors) / sizeof(colors[0]))];
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::cerr << "Usage: peer_to_peer_client <advertised-endpoint> [server-endpoint]\n"
                     "Example: peer_to_peer_client tcp://127.0.0.1:7001\n";
        return 2;
    }

    try {
        const std::string serverEndpoint = argc == 3 ? argv[2] : "tcp://127.0.0.1:5555";
        engine::networking::PeerClient peer(serverEndpoint, argv[1]);
        peer.start();

        // SDL and game state stay on this thread. PeerClient owns the socket worker.
        engine::Window window("peer_to_peer_demo", static_cast<int>(windowWidth),
                              static_cast<int>(windowHeight));
        engine::Renderer renderer(window);
        engine::InputHandler input;
        engine::ClientId self = 0;
        engine::networking::PlayerPosition local{80.f, 450.f};
        engine::networking::WorldSnapshot world{};
        bool haveWorld = false;
        std::unordered_map<engine::ClientId, engine::networking::PlayerPosition> remotePlayers;
        std::unordered_set<engine::ClientId> knownPeers;
        auto previousFrame = std::chrono::steady_clock::now();

        while (!window.shouldClose()) {
            window.pollEvents();
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::clamp(std::chrono::duration<float>(now - previousFrame).count(),
                                        0.f, 0.05f);
            previousFrame = now;

            using namespace engine::SC;
            if (input.isKeyPressed(SDL_SCANCODE_ESCAPE))
                break;
            const float dx = static_cast<float>(input.isKeyPressed(SDL_SCANCODE_D) ||
                                                input.isKeyPressed(SDL_SCANCODE_RIGHT)) -
                             static_cast<float>(input.isKeyPressed(SDL_SCANCODE_A) ||
                                                input.isKeyPressed(SDL_SCANCODE_LEFT));
            const float dy = static_cast<float>(input.isKeyPressed(SDL_SCANCODE_S) ||
                                                input.isKeyPressed(SDL_SCANCODE_DOWN)) -
                             static_cast<float>(input.isKeyPressed(SDL_SCANCODE_W) ||
                                                input.isKeyPressed(SDL_SCANCODE_UP));
            local.x = std::clamp(local.x + dx * playerSpeed * dt, 0.f, windowWidth - playerSize);
            local.y = std::clamp(local.y + dy * playerSpeed * dt, 0.f, windowHeight - playerSize);
            peer.publish(local);

            // Apply network events on the game thread. A world snapshot is also the current
            // peer directory, so peers that leave disappear without a separate leave event.
            for (const auto& event : peer.drain()) {
                if (event.kind == engine::networking::PeerEvent::Kind::Welcome) {
                    self = event.sender;
                    local = {80.f + 90.f * static_cast<float>((self - 1) % 8), 450.f};
                    std::cout << "Joined as peer " << self << '\n';
                } else if (event.kind == engine::networking::PeerEvent::Kind::World) {
                    world = event.world;
                    haveWorld = true;
                    std::unordered_set<engine::ClientId> active;
                    for (const auto& info : world.peers) {
                        if (info.id != self)
                            active.insert(info.id);
                    }
                    for (const auto id : active) {
                        if (!knownPeers.contains(id))
                            std::cout << "Peer " << id << " joined\n";
                    }
                    for (const auto id : knownPeers) {
                        if (!active.contains(id))
                            std::cout << "Peer " << id << " left\n";
                    }
                    knownPeers = std::move(active);
                    for (auto it = remotePlayers.begin(); it != remotePlayers.end();) {
                        if (!knownPeers.contains(it->first))
                            it = remotePlayers.erase(it);
                        else
                            ++it;
                    }
                } else if (event.kind == engine::networking::PeerEvent::Kind::Player) {
                    remotePlayers[event.sender] = event.position;
                }
            }

            renderer.clear({25, 31, 48, 255});
            if (haveWorld)
                renderer.fillRect({world.droneX, world.droneY}, {26.f, 26.f},
                                  {255, 220, 80, 255});
            for (const auto& [id, position] : remotePlayers)
                renderer.fillRect({position.x, position.y}, {playerSize, playerSize},
                                  peerColor(id));
            renderer.fillRect({local.x - 3.f, local.y - 3.f},
                              {playerSize + 6.f, playerSize + 6.f}, {255, 255, 255, 255});
            renderer.fillRect({local.x, local.y}, {playerSize, playerSize}, peerColor(self));
            renderer.present();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        peer.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Peer error: " << error.what() << '\n';
        return 1;
    }
}
