#include "demoPayload.hpp"

#include <engine/engine.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

namespace {
constexpr float width = 640.f;
constexpr float height = 480.f;
constexpr float size = 32.f;
constexpr float speed = 200.f;

engine::Color playerColor(engine::ClientId id) {
    constexpr engine::Color colors[] = {
        {80, 190, 255, 255}, {255, 115, 130, 255}, {140, 225, 140, 255},
        {200, 150, 255, 255}, {255, 175, 75, 255},
    };
    return colors[(id - 1) % (sizeof(colors) / sizeof(colors[0]))];
}
} // namespace

int main(int argc, char** argv) {
    try {
        engine::networking::ClientConfig config;
        config.peerToPeer = true;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--server" && i + 1 < argc) {
                const std::string endpoint = argv[++i];
                if (!engine::networking::validPeerEndpoint(endpoint))
                    throw std::invalid_argument("expected --server tcp://host:port");
                const auto colon = endpoint.rfind(':');
                config.serverHost = endpoint.substr(6, colon - 6);
                config.joinPort = static_cast<std::uint16_t>(std::stoi(endpoint.substr(colon + 1)));
            } else if (option == "--advertise" && i + 1 < argc) {
                config.advertisedHost = argv[++i];
            } else {
                throw std::invalid_argument(
                    "Usage: peer_to_peer_client [--server tcp://host:port] [--advertise host]");
            }
        }

        engine::networking::Client peer(config);
        peer.start();
        const auto self = peer.id();
        std::cout << "Joined as player " << static_cast<int>(self) << '\n';

        engine::Window window("Peer to peer demo", static_cast<int>(width),
                              static_cast<int>(height));
        engine::Renderer renderer(window);
        engine::InputHandler input;
        engine::Timeline realTime;
        engine::Timeline gameTime(realTime, 60);
        engine::Stepper stepper(gameTime);
        engine::Scene scene;
        const auto local = scene.createEntity();
        scene.transform(local).position = {60.f + 60.f * ((self - 1) % 8), 220.f};
        scene.addShape(local, {.size = {size, size}, .color = playerColor(self)});
        std::unordered_map<engine::ClientId, engine::EntityId> remotes;
        std::unordered_map<engine::ClientId, std::int64_t> lastTick;

        using Kind = engine::networking::ServerEvent::Kind;
        using namespace engine::SC;
        while (!window.shouldClose()) {
            window.pollEvents();
            const auto keyboard = engine::KeyboardState::capture(input);
            if (keyboard.isKeyPressed(SDL_SCANCODE_ESCAPE))
                break;

            // Network workers own the sockets. This loop owns the Scene and SDL.
            for (const auto& event : peer.drain()) {
                if (event.client == self || event.client == 0)
                    continue;
                if (event.kind == Kind::PlayerLeft) {
                    if (const auto it = remotes.find(event.client); it != remotes.end()) {
                        scene.destroyEntity(it->second);
                        remotes.erase(it);
                        lastTick.erase(event.client);
                        std::cout << "Player " << static_cast<int>(event.client) << " left\n";
                    }
                } else if (event.kind == Kind::Player) {
                    const auto position = netdemo::decode(event.payload);
                    if (!position || (lastTick.contains(event.client) &&
                                      event.tick <= lastTick.at(event.client)))
                        continue;
                    auto it = remotes.find(event.client);
                    if (it == remotes.end()) {
                        const auto id = scene.createEntity();
                        scene.addShape(id, {.size = {size, size},
                                            .color = playerColor(event.client)});
                        it = remotes.emplace(event.client, id).first;
                        std::cout << "Player " << static_cast<int>(event.client) << " appeared\n";
                    }
                    scene.transform(it->second).position = {position->x, position->y};
                    lastTick[event.client] = event.tick;
                }
            }

            stepper.beginFrame();
            while (stepper.step()) {
                const float dx = static_cast<float>(keyboard.isKeyPressed(SDL_SCANCODE_D) ||
                                                     keyboard.isKeyPressed(SDL_SCANCODE_RIGHT)) -
                                 static_cast<float>(keyboard.isKeyPressed(SDL_SCANCODE_A) ||
                                                     keyboard.isKeyPressed(SDL_SCANCODE_LEFT));
                const float dy = static_cast<float>(keyboard.isKeyPressed(SDL_SCANCODE_S) ||
                                                     keyboard.isKeyPressed(SDL_SCANCODE_DOWN)) -
                                 static_cast<float>(keyboard.isKeyPressed(SDL_SCANCODE_W) ||
                                                     keyboard.isKeyPressed(SDL_SCANCODE_UP));
                auto& position = scene.transform(local).position;
                position.x = std::clamp(position.x + dx * speed * gameTime.tickSeconds(),
                                        0.f, width - size);
                position.y = std::clamp(position.y + dy * speed * gameTime.tickSeconds(),
                                        0.f, height - size);
                peer.send(netdemo::encode({position.x, position.y}), stepper.tickIndex());
            }

            renderer.clear({25, 31, 48, 255});
            renderer.drawEntities(scene);
            renderer.present();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        peer.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Client error: " << error.what() << '\n';
        return 1;
    }
}
