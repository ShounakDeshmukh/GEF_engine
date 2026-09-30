#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <netinet/in.h>
#include <regex>
#include <set>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

bool portAvailable(int port) {
    const int socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket < 0)
        throw std::runtime_error("cannot create a port-check socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    const bool available =
        ::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    ::close(socket);
    return available;
}

int chooseRegistrationPort() {
    for (int attempt = 0; attempt < 100; ++attempt) {
        const int socket = ::socket(AF_INET, SOCK_STREAM, 0);
        if (socket < 0)
            throw std::runtime_error("cannot create a port-selection socket");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        const bool bound =
            ::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
        socklen_t length = sizeof(address);
        const bool named =
            bound && ::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) == 0;
        ::close(socket);
        if (!named)
            throw std::runtime_error("cannot allocate a registration port");

        const int port = ntohs(address.sin_port);
        if (port + 103 > 65535)
            continue;
        bool available = true;
        for (const int offset : {0, 1, 2, 3, 4, 100, 101, 102})
            available = portAvailable(port + offset) && available;
        if (available)
            return port;
    }
    throw std::runtime_error("cannot find ports for the network process test");
}

class Child {
public:
    Child(pid_t pid, int output) : pid_(pid), output_(output) {}
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    Child(Child&& other) noexcept
        : pid_(std::exchange(other.pid_, -1)), output_(std::exchange(other.output_, -1)) {}
    Child& operator=(Child&& other) noexcept {
        if (this != &other) {
            stop();
            pid_ = std::exchange(other.pid_, -1);
            output_ = std::exchange(other.output_, -1);
        }
        return *this;
    }
    ~Child() { stop(); }

    void stop() noexcept {
        if (pid_ > 0) {
            int status = 0;
            pid_t result;
            do {
                result = ::waitpid(pid_, &status, WNOHANG);
            } while (result < 0 && errno == EINTR);
            if (result == 0) {
                ::kill(pid_, SIGTERM);
                do {
                    result = ::waitpid(pid_, &status, 0);
                } while (result < 0 && errno == EINTR);
            }
            pid_ = -1;
        }
        if (output_ >= 0) {
            ::close(output_);
            output_ = -1;
        }
    }

    std::string waitFor(std::chrono::seconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            int status = 0;
            const pid_t result = ::waitpid(pid_, &status, WNOHANG);
            if (result == pid_) {
                pid_ = -1;
                std::string output;
                std::array<char, 512> buffer{};
                while (true) {
                    const ssize_t count = ::read(output_, buffer.data(), buffer.size());
                    if (count > 0)
                        output.append(buffer.data(), static_cast<std::size_t>(count));
                    else if (count == 0)
                        break;
                    else if (errno != EINTR)
                        throw std::runtime_error("cannot read child output");
                }
                ::close(output_);
                output_ = -1;
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                    throw std::runtime_error("network probe failed: " + output);
                return output;
            }
            if (result < 0 && errno != EINTR)
                throw std::runtime_error("cannot wait for network probe");
            std::this_thread::sleep_for(20ms);
        }
        throw std::runtime_error("network probe timed out");
    }

private:
    pid_t pid_ = -1;
    int output_ = -1;
};

Child spawnProbe(const std::string& path, std::vector<std::string> arguments) {
    arguments.insert(arguments.begin(), path);
    std::vector<char*> command;
    for (auto& argument : arguments)
        command.push_back(argument.data());
    command.push_back(nullptr);

    int fds[2];
    if (::pipe(fds) != 0)
        throw std::runtime_error("cannot create a child output pipe");
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        throw std::runtime_error("cannot start a network probe");
    }
    if (pid == 0) {
        ::close(fds[0]);
        ::dup2(fds[1], STDOUT_FILENO);
        ::dup2(fds[1], STDERR_FILENO);
        ::close(fds[1]);
        ::execv(path.c_str(), command.data());
        _exit(127);
    }
    ::close(fds[1]);
    return Child(pid, fds[0]);
}

std::set<int> parseIds(const std::string& text) {
    std::set<int> ids;
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find(',', start);
        ids.insert(std::stoi(text.substr(start, end - start)));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return ids;
}

struct Result {
    int self;
    bool world;
    bool moving;
    bool sharedClock;
    std::set<int> seen;
    std::set<int> left;
};

Result parseResult(std::string output) {
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();
    static const std::regex pattern(
        R"(self=(\d+) world=(\d+) moving=(\d+) sharedClock=(\d+) seen=([\d,]*) left=([\d,]*))");
    std::smatch match;
    if (!std::regex_match(output, match, pattern))
        throw std::runtime_error("unexpected network probe output: " + output);
    return {std::stoi(match[1]),      std::stoi(match[2]) != 0, std::stoi(match[3]) != 0,
            std::stoi(match[4]) != 0, parseIds(match[5]),       parseIds(match[6])};
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: network_process_test <network_probe>\n";
        return 2;
    }
    try {
        const int registration = chooseRegistrationPort();
        const int control = registration + 1;
        const std::string probe = argv[1];
        Child server =
            spawnProbe(probe, {"server", std::to_string(registration), std::to_string(control)});
        std::this_thread::sleep_for(300ms);

        std::vector<Child> peers;
        peers.reserve(3);
        const auto startPeer = [&](int index, int rate, int duration) {
            peers.push_back(
                spawnProbe(probe, {"peer", std::to_string(registration), std::to_string(control),
                                   std::to_string(registration + 100 + index), std::to_string(rate),
                                   std::to_string(duration)}));
        };
        startPeer(0, 5, 6500);
        startPeer(1, 20, 2500);
        std::this_thread::sleep_for(1s);
        startPeer(2, 40, 4500);

        std::array<Result, 3> results{};
        for (std::size_t i = 0; i < peers.size(); ++i) {
            const auto output = peers[i].waitFor(10s);
            std::cout << output;
            results[i] = parseResult(output);
        }

        const std::set<int> ids{results[0].self, results[1].self, results[2].self};
        if (ids != std::set<int>{1, 2, 3})
            throw std::runtime_error("peers did not receive distinct IDs");
        for (const auto& result : results) {
            if (!result.world || !result.moving || !result.sharedClock)
                throw std::runtime_error("shared world state did not stay consistent");
            auto otherIds = ids;
            otherIds.erase(result.self);
            if (result.seen != otherIds)
                throw std::runtime_error("a peer did not receive all direct player updates");
        }
        if (!results[0].left.contains(results[1].self))
            throw std::runtime_error("the first peer did not notice the second peer leaving");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
