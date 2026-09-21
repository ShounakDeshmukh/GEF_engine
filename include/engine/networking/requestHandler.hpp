#pragma once 

#include <string>
#include <memory>
#include <utility>
#include <mutex>

#include "networkShared.hpp"

namespace engine::networking {

    
    class connectionManager;

    /** Client side request-reply handler */
    class requestHandler {
    
        public:
        requestHandler(std::string connectionString);
        ~requestHandler();
    
        /** threadsafe */
        void send(std::string txt);

        //** threadsafe. Returns the requested data and an "isValidReply" bool in case of failure*/
        template <typename T, typename U>
        std::pair<T, NetworkError> send(U msg)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            static_assert(std::is_trivially_copyable_v<U>);
            static_assert(std::is_trivially_copyable_v<ResponsePacket<T>>);

            ResponsePacket<T> reply {};
            bool valid = sendAndReceive(&msg, sizeof(U), &reply, sizeof(reply));
            if(!valid){reply.errorCode = NetworkError::ReceiveFailed;}
            return {reply.data, reply.errorCode};

        }
    
        private:
        std::unique_ptr<connectionManager> connection_;
        std::mutex mutex_;

        bool sendAndReceive(void* request, std::size_t reqSize, void* reply, std::size_t repSize);
    
    
    };

}
