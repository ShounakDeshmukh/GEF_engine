// Times client-server round trips for each snapshot format over a fixed experiment matrix.
// Usage: netbench [--iterations I] [--runs R] [--out raw.csv]

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <engine/entity.hpp>
#include <engine/log.hpp>
#include <engine/networking/sessionClient.hpp>
#include <engine/networking/sessionServer.hpp>
#include <exception>
#include <fmt/core.h>
#include <fmt/os.h>
#include <latch>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace net = engine::networking;
using Clock = std::chrono::steady_clock;

namespace {

struct Condition {
    std::string_view experiment;
    int clients;
    int moving;
    int statics;
};

constexpr Condition kMatrix[] = {
    {"moving", 2, 10, 0},   {"moving", 2, 100, 0},  {"moving", 2, 500, 0}, {"clients", 1, 100, 0},
    {"clients", 2, 100, 0}, {"clients", 4, 100, 0}, {"static", 2, 100, 0}, {"static", 2, 100, 500},
};

struct Format {
    net::SnapshotFormat format;
    std::string_view name;
};

constexpr Format kFormats[] = {{net::SnapshotFormat::Binary, "binary"},
                               {net::SnapshotFormat::Json, "json"}};

constexpr auto kSnapshotTimeout = std::chrono::seconds(5);
constexpr float kWrapX = 10000.f;

struct RunResult {
    double seconds;
    std::size_t snapshotBytes;
};

engine::EntityId addBox(engine::Scene& scene, bool moving) {
    const engine::EntityId id = scene.createEntity();
    scene.addCollider(id, {{32.f, 32.f}});
    scene.addShape(id, {.size = {32.f, 32.f}});
    if (moving) {
        scene.addRigidBody(id, {{1.f, 0.f}});
    }
    return id;
}

/** One client: `iterations` round trips of submitting its player and applying a snapshot. */
bool runClient(int port, net::SnapshotFormat format, int iterations, std::latch& start,
               Clock::time_point& finished) {
    net::sessionClient client(fmt::format("tcp://127.0.0.1:{}", port));
    if (!client.join()) {
        engine::log::error("netbench: client failed to join");
        start.count_down();
        return false;
    }
    client.replicator().setFormat(format);
    engine::Scene scene;
    const engine::EntityId player = addBox(scene, true);
    client.replicator().track(player);
    client.start();

    start.arrive_and_wait();
    for (int i = 0; i < iterations; ++i) {
        scene.transform(player).position.x += 1.f;
        client.submitScene(scene, i);
        const auto deadline = Clock::now() + kSnapshotTimeout;
        while (!client.applySnapshot(scene)) {
            if (Clock::now() > deadline) {
                engine::log::error("netbench: client {} got no snapshot at iteration {}",
                                   client.id(), i);
                client.leave();
                return false;
            }
            std::this_thread::yield();
        }
    }
    finished = Clock::now();
    client.leave();
    return true;
}

RunResult runOnce(const Condition& condition, net::SnapshotFormat format, int iterations) {
    net::sessionServer server("tcp://*:0");
    server.replicator().setFormat(format);
    server.start();

    engine::Scene scene;
    std::vector<engine::EntityId> moving;
    moving.reserve(condition.moving);
    for (int i = 0; i < condition.moving; ++i) {
        moving.push_back(addBox(scene, true));
        server.replicator().track(moving.back());
    }
    for (int i = 0; i < condition.statics; ++i) {
        server.replicator().track(addBox(scene, false));
    }

    std::latch start(condition.clients + 1);
    std::atomic<int> running = condition.clients;
    std::atomic<bool> failed = false;
    std::vector<Clock::time_point> finished(condition.clients);
    std::vector<std::thread> clients;
    clients.reserve(condition.clients);
    for (int i = 0; i < condition.clients; ++i) {
        clients.emplace_back([&, i] {
            if (!runClient(server.port(), format, iterations, start, finished[i])) {
                failed = true;
            }
            --running;
        });
    }

    start.arrive_and_wait();
    const auto begin = Clock::now();
    for (std::int64_t tick = 1; running > 0; ++tick) {
        server.applyClientStates(scene);
        for (const engine::EntityId id : moving) {
            float& x = scene.transform(id).position.x;
            x = x + 1.f >= kWrapX ? 0.f : x + 1.f;
        }
        server.publishScene(scene, tick);
    }
    for (std::thread& client : clients) {
        client.join();
    }
    const std::size_t snapshotBytes = server.replicator().encodeAll(scene).size();
    server.stop();

    if (failed) {
        throw std::runtime_error("a client failed; see the log above");
    }
    const auto end = *std::max_element(finished.begin(), finished.end());
    return {std::chrono::duration<double>(end - begin).count(), snapshotBytes};
}

} // namespace

int main(int argc, char** argv) try {
    engine::log::init();
    int iterations = 10000;
    int runs = 5;
    std::string outPath = "raw.csv";
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string_view flag = argv[i];
        if (flag == "--iterations") {
            iterations = std::stoi(argv[i + 1]);
        } else if (flag == "--runs") {
            runs = std::stoi(argv[i + 1]);
        } else if (flag == "--out") {
            outPath = argv[i + 1];
        } else {
            throw std::invalid_argument(fmt::format("unknown flag {}", flag));
        }
    }
    if (argc % 2 == 0) {
        throw std::invalid_argument("usage: netbench [--iterations I] [--runs R] [--out raw.csv]");
    }

    auto out = fmt::output_file(outPath);
    out.print("experiment,format,clients,moving,static,iterations,run,seconds,ms_per_iter,"
              "snapshot_bytes\n");
    for (const Condition& c : kMatrix) {
        // Formats alternate within each run so drift (thermals, background load) hits both.
        for (int run = 1; run <= runs; ++run) {
            for (const Format& f : kFormats) {
                const RunResult result = runOnce(c, f.format, iterations);
                const double msPerIter = result.seconds * 1000.0 / iterations;
                fmt::print("{} c={} m={} s={} {} run {}/{}: {:.3f} s ({:.3f} ms/iter, {} B)\n",
                           c.experiment, c.clients, c.moving, c.statics, f.name, run, runs,
                           result.seconds, msPerIter, result.snapshotBytes);
                out.print("{},{},{},{},{},{},{},{:.6f},{:.6f},{}\n", c.experiment, f.name,
                          c.clients, c.moving, c.statics, iterations, run, result.seconds,
                          msPerIter, result.snapshotBytes);
            }
        }
    }
    return 0;
} catch (const std::exception& error) {
    engine::log::error("netbench: {}", error.what());
    return 1;
}
