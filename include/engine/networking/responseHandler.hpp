#pragma once 

#include <string>
#include <memory>

#include <functional>
#include <atomic>
#include <iostream>

#include "networkShared.hpp"


namespace engine::networking {

    
    class connectionManager;

    /** Server side request-reply handler */
    class responseHandler {
        public:
        responseHandler(std::string connectionString);
        ~responseHandler();

        /** Enforces single thread usage */
        void run();
        
        /** Enforces single thread usage */
        void run(std::function<std::string(std::string)> func);

        /** Enforces single thread usage */
        template <typename T, typename U, typename Func>
        void run(Func&& func)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            static_assert(std::is_trivially_copyable_v<U>);
            static_assert(std::is_trivially_copyable_v<ResponsePacket<T>>);

            if(running_.exchange(true))
            {
                std::cerr << "responseHandler already running" << std::endl;
                return;
            }
            while(!stop_.load())
            {
                U requestData {};
                auto status = receive(&requestData, sizeof(U));

                //if no message received, safe to loop
                if(status == ReceivedStatus::NoMessage){continue;}

                ResponsePacket<T> reply{};

                if(status == ReceivedStatus::InvalidSize)
                {
                    reply.errorCode = NetworkError::InvalidRequestSize;
                }
                else 
                {
                    try
                    {
                        reply.data = std::invoke(std::forward<Func>(func), requestData);
                        reply.errorCode = NetworkError::None;
                    }
                    catch (...)
                    {
                        reply.errorCode = NetworkError::HandlerError;
                    }
                }
                send(&reply, sizeof(reply));
            }
            running_.store(false);

        }

        void stop()
        {
            stop_.store(true);
        }

        void setReceiveTimeout(int milliseconds);

        private:
        std::unique_ptr<connectionManager> connection_;
        std::atomic<bool> running_{false};
        std::atomic<bool> stop_{false};

        /** returns true if data is properly received */
        ReceivedStatus receive(void* data, std::size_t size);
        void send(const void* data, std::size_t size);


    };


}
