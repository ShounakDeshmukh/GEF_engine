#pragma once
#include <zmq.hpp>

namespace engine::networking {

    inline zmq::context_t& getContext() {
        static zmq::context_t context(1);
        return context;
    }


    class connectionManager{
        public:
            connectionManager(zmq::socket_type socket_type):
            sck{getContext(), socket_type} {
                // A timed-out request may still have an unsent frame. Do not let
                // that frame block socket or context shutdown indefinitely.
                sck.set(zmq::sockopt::linger, 0);
            }

        zmq::socket_t sck; 
    };

} // namespace engine::networking
