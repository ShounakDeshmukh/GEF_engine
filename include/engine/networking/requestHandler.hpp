#pragma once 

#include <string>
#include <memory>
#include <utility>
#include <mutex>

#include "networkShared.hpp"
#include "bytes.hpp"

namespace engine::networking {

    
    class connectionManager;

    /** Client side request-reply handler */
    class requestHandler {
    
        public:
        requestHandler(std::string connectionString);
        ~requestHandler();
    
        /** threadsafe */
        std::string send(std::string txt);

        
        //** threadsafe. Returns the requested data and an "isValidReply" bool in case of failure
        // Fixed size message and response*/
        template <typename T, typename U>
        std::pair<T, bool> send(U msg)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            static_assert(std::is_trivially_copyable_v<U>);

            T reply {};
            auto errorCode = sendAndReceive(&msg, sizeof(U), &reply, sizeof(reply));
            bool valid = (errorCode == NetworkError::None);
            //TODO: add logging on error
            return {reply, valid};

        }


        /** threadsafe. Variable sized message and response, efficiency version */
        bool send(ByteView msg, Bytes& response);
        bool send(const Bytes& msg, Bytes& response);


        /** threadsafe. Variable sized message and response, matching fixed sized style */
        std::pair<Bytes, bool> send(ByteView msg);
        std::pair<Bytes, bool> send(const Bytes& msg);

        void setReplyTimeout(int milliseconds);

        private:
        std::unique_ptr<connectionManager> connection_;
        std::mutex mutex_;

        NetworkError sendAndReceive(const void* request, std::size_t reqSize, Bytes& reply);
        NetworkError sendAndReceive(const void* request, std::size_t reqSize, void* reply, std::size_t repSize);
    
    
    };

}
