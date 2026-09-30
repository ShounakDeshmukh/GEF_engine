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
                // default is to wait forever for unsent messages, which hangs the
                // context shutdown at exit when a peer is unreachable
                sck.set(zmq::sockopt::linger, kLingerMs);
            }

        static constexpr int kLingerMs = 200;

        zmq::socket_t sck; 
    };

}
