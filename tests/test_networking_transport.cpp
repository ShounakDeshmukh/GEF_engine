#include "networkTestUtil.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <engine/networking/publisher.hpp>
#include <engine/networking/requestHandler.hpp>
#include <engine/networking/responseHandler.hpp>
#include <engine/networking/subscriber.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using namespace std::chrono_literals;
using engine::networking::Bytes;
using engine::networking::publisher;
using engine::networking::ReceivedStatus;
using engine::networking::requestHandler;
using engine::networking::responseHandler;
using engine::networking::subscriber;

namespace {

/** Runs a responseHandler loop on its own thread; stops and joins it on destruction, so a
 *  failed REQUIRE does not leave a joinable thread behind. */
class handlerThread {
public:
    template <typename Run>
    handlerThread(responseHandler& handler, Run run) : handler_(handler), thread_(std::move(run)) {}
    ~handlerThread() { stop(); }

    void stop() {
        handler_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    responseHandler& handler_;
    std::thread thread_;
};

std::string echo(const std::string& in) {
    if (in == "boom") {
        throw std::runtime_error("boom");
    }
    return "echo:" + in;
}

} // namespace

TEST_CASE("responseHandler replies to a string request", "[net][transport]") {
    responseHandler server("tcp://*:0");
    server.setReceiveTimeout(50);
    handlerThread running(server, [&server] { server.run(echo); });
    requestHandler client(testutil::localEndpoint(server.port()));

    REQUIRE(client.send(std::string("hi")) == "echo:hi");
}

TEST_CASE("A handler exception yields an empty reply and the handler keeps serving",
          "[net][transport]") {
    responseHandler server("tcp://*:0");
    server.setReceiveTimeout(50);
    handlerThread running(server, [&server] { server.run(echo); });
    requestHandler client(testutil::localEndpoint(server.port()));

    REQUIRE(client.send(std::string("boom")).empty());
    REQUIRE(client.send(std::string("ok")) == "echo:ok");
}

TEST_CASE("requestHandler recovers after a reply timeout", "[net][transport]") {
    responseHandler server("tcp://*:0");
    server.setReceiveTimeout(50);
    int calls = 0; // server thread only
    handlerThread running(server, [&server, &calls] {
        server.run<int, int>([&calls](int value) {
            if (calls++ == 0) {
                std::this_thread::sleep_for(300ms);
            }
            return value + 1;
        });
    });
    requestHandler client(testutil::localEndpoint(server.port()));
    client.setReplyTimeout(100);

    auto [first, firstValid] = client.send<int, int>(1);
    REQUIRE_FALSE(firstValid);

    std::this_thread::sleep_for(400ms);
    auto [second, secondValid] = client.send<int, int>(41);
    REQUIRE(secondValid);
    REQUIRE(second == 42);
}

TEST_CASE("responseHandler::stop returns promptly with a receive timeout", "[net][transport]") {
    responseHandler server("tcp://*:0");
    server.setReceiveTimeout(50);
    handlerThread running(server, [&server] { server.run(); });
    std::this_thread::sleep_for(20ms);

    const auto start = std::chrono::steady_clock::now();
    running.stop();

    REQUIRE(std::chrono::steady_clock::now() - start < 1s);
}

TEST_CASE("subscriber reports NoMessage when its receive timeout expires", "[net][transport]") {
    subscriber sub;
    sub.setReceiveTimeout(50);
    Bytes message;

    const auto start = std::chrono::steady_clock::now();
    const auto status = sub.listenBytes(message);

    REQUIRE(status == ReceivedStatus::NoMessage);
    REQUIRE(std::chrono::steady_clock::now() - start < 500ms);
}

TEST_CASE("publisher sends a string literal without a trailing NUL", "[net][transport]") {
    publisher pub("tcp://*:0");
    subscriber sub(testutil::localEndpoint(pub.port()), "t/");
    sub.setReceiveTimeout(50);
    std::this_thread::sleep_for(200ms); // slow joiner
    std::string received;

    // republished until received, in case the subscription is still propagating
    REQUIRE(testutil::eventually([&] {
        pub.publish("abc", "t/");
        return sub.listen(received) == ReceivedStatus::Success;
    }));

    REQUIRE(received == "abc");
    REQUIRE(received.size() == 3);
}
