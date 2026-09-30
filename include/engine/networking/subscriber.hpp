#pragma once 

#include <string>
#include <memory>
#include <atomic>

#include "bytes.hpp"

namespace engine::networking {

    class connectionManager;

    //** Create a subscriber per topic */
    class subscriber {
        public:
        subscriber(std::string connectionString = "", std::string topic = "");
        ~subscriber();

        bool connect(const std::string& endpoint);
        bool disconnect(const std::string& endpoint);

        /** Enforces single thread usage */
        std::string listen();

        /** Enforces single thread usage. Variable sized message */
        Bytes listenBytes();

        /** Enforces single thread usage */
        template<typename T>
        T listenT()
        {
            static_assert(std::is_trivially_copyable_v<T>, "requires a trivially copyable datatype");
            T result{};
            try {
                receive(&result, sizeof(T));
            } catch (...)
            {
                throw std::runtime_error("Received malformed message");
            }

            return result;
        }

        private:
        std::unique_ptr<connectionManager> connection_;
        std::atomic<bool> running_{false};

        void receive(void* data, std::size_t size);

        /** RAII for tracking if loop is running */
        struct RunningGuard
        {
            std::atomic_bool& running;

            ~RunningGuard()
            {
                running.store(false);
            }
        };


    };

}
