// Snapshot formats: the same scene encoded as Binary and as JSON, and applied by a receiver that
// reads either. In a session, set the format on the sender's replicator:
//     server.replicator().setFormat(engine::networking::SnapshotFormat::Json);
//     client.replicator().setFormat(engine::networking::SnapshotFormat::Json);
#include <engine/engine.hpp>
#include <string>
#include <utility>

namespace net = engine::networking;

int main() {
    engine::log::init();

    engine::Scene serverScene;
    net::sceneReplicator server(engine::kServerId);
    for (int i = 0; i < 3; ++i) {
        const engine::EntityId box = serverScene.createEntity();
        serverScene.transform(box).position = {static_cast<float>(i) * 100.f, 50.f};
        serverScene.addShape(box, {.size = {32.f, 32.f}, .color = {200, 50, 50, 255}});
        server.track(box);
    }

    engine::Scene clientScene;
    net::sceneReplicator client(1);

    for (const auto [format, name] : {std::pair{net::SnapshotFormat::Binary, "binary"},
                                      std::pair{net::SnapshotFormat::Json, "json"}}) {
        server.setFormat(format);
        const net::Bytes snapshot = server.encodeAll(serverScene);
        const bool applied = client.apply(clientScene, snapshot, engine::kServerId);
        engine::log::info("{}: {} bytes, applied = {}, client entities = {}", name, snapshot.size(),
                          applied, clientScene.transforms().size());
    }

    // JSON is plain text, so it can be logged as is.
    server.setFormat(net::SnapshotFormat::Json);
    const net::Bytes json = server.encodeAll(serverScene);
    engine::log::info("{}", std::string(reinterpret_cast<const char*>(json.data()), json.size()));
    return 0;
}
