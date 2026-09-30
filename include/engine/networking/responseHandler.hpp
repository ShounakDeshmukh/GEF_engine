#pragma once 

#include <string>
#include <memory>

#include <functional>
#include <atomic>
#include <iostream>

#include "networkShared.hpp"
#include "bytes.hpp"

namespace engine::networking {

    
    class connectionManager;

    /** Server side request-reply handler */
    class responseHandler {
        public:
        responseHandler(std::string connectionString);

        /** stop() and join the thread inside run() first. */
        ~responseHandler();

        /** Enforces single thread usage */
        void run();
        
        /** Enforces single thread usage */
        void run(std::function<std::string(std::string)> func);

        /** Enforces single thread usage */
        template <typename T, typename U, typename Func>
        void run(Func&& func)
        {
            if(running_.exchange(true))
            {
                std::cerr << "responseHandler already running" << std::endl;
                return;
            }
            RunningGuard guard{running_, stop_};

            while(!stop_.load())
            {
                U requestData {};
                auto status = receive(requestData);

                //if no message received, safe to loop
                //TODO: add logging
                if(status == ReceivedStatus::NoMessage){continue;}
                if(status == ReceivedStatus::Closed){break;}

                T replyData{};
                auto errorCode = NetworkError::None;

                if(status == ReceivedStatus::InvalidSize)
                {
                    errorCode = NetworkError::InvalidRequestSize;
                }
                else 
                {
                    try
                    {
                        replyData = std::invoke(std::forward<Func>(func), requestData);
                    }
                    catch (...)
                    {
                        errorCode = NetworkError::HandlerError;
                    }
                }
                send(replyData, errorCode);
            }

        }

        /** threadsafe. Takes effect between requests, so without setReceiveTimeout() a
         *  run() blocked waiting for a request never returns. A stop() before run()
         *  makes that run() return immediately. */
        void stop() {if(stop_.load() == false) {stop_.store(true);}}

        /** Call before run(), not while it is running. Required for stop() to take effect
         *  when no requests arrive. */
        void setReceiveTimeout(int milliseconds);

        int port() const {return port_;}

        private:
        std::unique_ptr<connectionManager> connection_;
        std::atomic<bool> running_{false};
        std::atomic<bool> stop_{false};
        int port_{-1};

        //Actual implementation with underlying library
        ReceivedStatus receiveRaw(void* data, std::size_t size);
        ReceivedStatus receiveRaw(Bytes& data);
        void sendRaw(const void* data, std::size_t size, NetworkError err);

        /** Fixed Size data retrieval */
        template <typename T>
        ReceivedStatus receive(T& data)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            return receiveRaw(&data, sizeof(T));
        }

        /** Fixed Size data sending */
        template <typename T>
        void send(const T& data, NetworkError err)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            sendRaw(&data, sizeof(T), err);
        }

        /** Variable Size data retrieval */
        ReceivedStatus receive(Bytes& data)
        {
            return receiveRaw(data);
        }

        /** Variable Size data sending */
        void send(const Bytes& data, NetworkError err)
        {
            sendRaw(data.data(), data.size(), err);
        }



        /** RAII for tracking if loop is running. Clears stop on exit, so a stop()
         *  issued before run() is consumed by that run() rather than lost. */
        struct RunningGuard
        {
            std::atomic_bool& running;
            std::atomic_bool& stop;

            ~RunningGuard()
            {
                stop.store(false);
                running.store(false);
            }
        };

    };


}
