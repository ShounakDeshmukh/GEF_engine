#pragma once 

#include <string>
#include <memory>
#include <mutex>
#include <type_traits>

#include "bytes.hpp"

namespace engine::networking {

    class connectionManager;

    /** Publish to multiple topics per connection.
     *
     *  Delivery is best effort: subscribers that are still connecting miss earlier
     *  messages, and messages are dropped once a slow subscriber's queue fills (1000 by
     *  default). Suited to state that is republished every tick, not one-off events.
     *  Subscribers filter by topic prefix, so end topics with a delimiter ("player/1/"). */
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

        /** threadsafe. Pointers and arrays are excluded so string literals and char*
         *  resolve to the std::string overload instead of sending raw pointer bytes. */
        template <typename T>
            requires (!std::is_pointer_v<T> && !std::is_array_v<T>)
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
