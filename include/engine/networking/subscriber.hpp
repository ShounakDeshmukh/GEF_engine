#pragma once 

#include <string>
#include <memory>
#include <atomic>
#include <type_traits>
#include <utility>

#include "networkShared.hpp"
#include "bytes.hpp"

namespace engine::networking {

    class connectionManager;

    /** Create a subscriber per topic.
     *
     *  Owned by a single thread: every call, including connect() and disconnect(), must
     *  come from the thread that listens. Other threads that need the same topic should
     *  create their own subscriber. */
    class subscriber {
        public:
        /** topic is a prefix filter: "player/1" also receives "player/10". End topics
         *  with a delimiter ("player/1/") to match exactly. "" receives every topic.
         *  Messages published before the connection completes are not received. */
        subscriber(std::string connectionString = "", std::string topic = "");

        /** Must not run while another thread is still inside listen(). */
        ~subscriber();

        /** Owning thread only; not while a listen() call on this subscriber is blocked. */
        bool connect(const std::string& endpoint);
        /** Owning thread only. endpoint must match the string passed to connect(). */
        bool disconnect(const std::string& endpoint);

        /** Owning thread only. How long each listen waits for a message; -1 (the
         *  default) waits forever. Needed to stop a listening thread, and to call
         *  connect()/disconnect() between listens. */
        void setReceiveTimeout(int milliseconds);

        /** Enforces single thread usage. Returns "" on timeout, indistinguishable from an
         *  empty message; use listen(std::string&) to tell them apart. */
        std::string listen();

        /** Enforces single thread usage. NoMessage on timeout or interruption */
        ReceivedStatus listen(std::string& message);

        /** Enforces single thread usage. Variable sized message, {} on timeout */
        Bytes listenBytes();

        /** Enforces single thread usage. Variable sized message */
        ReceivedStatus listenBytes(Bytes& message);

        /** Enforces single thread usage. Returns the message and an "isValid" bool, false on
         *  timeout, a malformed message, or a concurrent listen from another thread */
        template<typename T>
        std::pair<T, bool> listenT()
        {
            T result{};
            bool valid = (listenT(result) == ReceivedStatus::Success);
            return {result, valid};
        }

        /** Enforces single thread usage. InvalidSize if the message is not sizeof(T) */
        template<typename T>
        ReceivedStatus listenT(T& message)
        {
            static_assert(std::is_trivially_copyable_v<T>, "requires a trivially copyable datatype");
            return receive(&message, sizeof(T));
        }

        private:
        std::unique_ptr<connectionManager> connection_;
        std::atomic<bool> running_{false};

        ReceivedStatus receive(void* data, std::size_t size);
        ReceivedStatus receive(Bytes& data);

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
