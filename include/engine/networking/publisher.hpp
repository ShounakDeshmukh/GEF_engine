#pragma once 

#include <string>
#include <memory>
#include <mutex>

#include "bytes.hpp"

namespace engine::networking {

    class connectionManager;

    /** Publish to multiple topics per connection */
    class publisher {
        public:
        publisher(std::string connectionString);
        ~publisher();

        /** threadsafe */
        void publish(std::string input, std::string topic = "");

        /** threadsafe. Variable sized message */
        void publish(ByteView data, const std::string& topic = "");
        void publish(const Bytes& data, const std::string& topic = "");

        int port() const {return port_;}

        /** threadsafe */
        template <typename T> 
        void publish(const T& data, std::string topic = "")
        {
            static_assert(std::is_trivially_copyable_v<T>, "requires a trivially copyable datatype");
            send(&data, sizeof(T), topic);
        } 

        private:
        std::unique_ptr<connectionManager> connection_;
        std::mutex mutex_;
        int port_{-1};

        void send(const void* data, std::size_t size, std::string topic);

    };

}
