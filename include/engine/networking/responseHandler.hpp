#pragma once 

#include <string>
#include <memory>

#include <functional>
#include <atomic>
#include <iostream>


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

            if(running_.exchange(true))
            {
                std::cerr << "responseHandler already running" << std::endl;
                return;
            }
            while(true)
            {
                U requestData {};
                auto datavalid = receive(&requestData, sizeof(U));
                if(datavalid)
                {
                    T replyData = std::invoke(std::forward<Func>(func), requestData);
                    send(&replyData, sizeof(T));
                }
            }
            running_.store(false);

        }


        private:
        std::unique_ptr<connectionManager> connection_;
        std::atomic<bool> running_{false};

        /** returns true if data is properly received */
        bool receive(void* data, std::size_t size);
        void send(const void* data, std::size_t size);


    };


}
