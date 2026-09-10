#include "engine/networking.hpp"

#include <zmq.hpp>

#include <future>
#include <iostream>
#include <string>
#include <thread>

// #include "zmq.hpp"
#include <zmq_addon.hpp>

#include <atomic>


void PublisherThread(zmq::context_t *ctx, std::atomic<bool>& flag)
{
    //  Prepare publisher
    zmq::socket_t publisher(*ctx, zmq::socket_type::pub);
    publisher.bind("inproc://#1");

    // Give the subscribers a chance to connect, so they don't lose any messages
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    while (flag) {
        //  Write three messages, each with an envelope and content
        publisher.send(zmq::str_buffer("A"), zmq::send_flags::sndmore);
        publisher.send(zmq::str_buffer("Message in A envelope"));
        publisher.send(zmq::str_buffer("B"), zmq::send_flags::sndmore);
        publisher.send(zmq::str_buffer("Message in B envelope"));
        publisher.send(zmq::str_buffer("C"), zmq::send_flags::sndmore);
        publisher.send(zmq::str_buffer("Message in C envelope"));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void SubscriberThread1(zmq::context_t *ctx, std::atomic<bool>& flag)
{
    //  Prepare subscriber
    zmq::socket_t subscriber(*ctx, zmq::socket_type::sub);
    subscriber.connect("inproc://#1");

    //  Thread2 opens "A" and "B" envelopes
    subscriber.set(zmq::sockopt::subscribe, "A");
    subscriber.set(zmq::sockopt::subscribe, "B");
    subscriber.set(zmq::sockopt::rcvtimeo, 100);

    while (flag) {
        // Receive all parts of the message
        std::vector<zmq::message_t> recv_msgs;
        zmq::recv_result_t result =
          zmq::recv_multipart(subscriber, std::back_inserter(recv_msgs));
        // assert(result && "recv failed");
        // assert(*result == 2);

        if(result)
        {
            std::cout << "Thread2: [" << recv_msgs[0].to_string() << "] "
                      << recv_msgs[1].to_string() << std::endl;

        }
    }
}

void SubscriberThread2(zmq::context_t *ctx, std::atomic<bool>& flag)
{
    //  Prepare our context and subscriber
    zmq::socket_t subscriber(*ctx, zmq::socket_type::sub);
    subscriber.connect("inproc://#1");

    //  Thread3 opens ALL envelopes
    subscriber.set(zmq::sockopt::subscribe, "");
    subscriber.set(zmq::sockopt::rcvtimeo, 100);

    while (flag) {
        // Receive all parts of the message
        std::vector<zmq::message_t> recv_msgs;
        zmq::recv_result_t result =
          zmq::recv_multipart(subscriber, std::back_inserter(recv_msgs));
        // assert(result && "recv failed");
        // assert(*result == 2);

        if(result)
        {
            std::cout << "Thread3: [" << recv_msgs[0].to_string() << "] "
                      << recv_msgs[1].to_string() << std::endl;
        }
    }
}

void flagTimeout(std::atomic<bool>& flag)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    flag = false;
}



namespace engine {
    int tester::testFunction()
    {
        // zmq::context_t ctx;
        // zmq::socket_t sock(ctx, zmq::socket_type::push);
        // sock.bind("inproc://test");
        // sock.send(zmq::str_buffer("hello, world"), zmq::send_flags::dontwait);

            /*
     * No I/O threads are involved in passing messages using the inproc transport.
     * Therefore, if you are using a ØMQ context for in-process messaging only you
     * can initialise the context with zero I/O threads.
     *
     * Source: http://api.zeromq.org/4-3:zmq-inproc
     */

     std::atomic<bool> flag = true;

    zmq::context_t ctx(0);

    auto thread1 = std::async(std::launch::async, PublisherThread, &ctx, std::ref(flag));

    // Give the publisher a chance to bind, since inproc requires it
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto thread2 = std::async(std::launch::async, SubscriberThread1, &ctx, std::ref(flag));
    auto thread3 = std::async(std::launch::async, SubscriberThread2, &ctx, std::ref(flag));
    auto thread4 = std::async(std::launch::async, flagTimeout, std::ref(flag));
    thread1.wait();
    thread2.wait();
    thread3.wait();
    thread4.wait();

    /*
     * Output:
     *   An infinite loop of a mix of:
     *     Thread2: [A] Message in A envelope
     *     Thread2: [B] Message in B envelope
     *     Thread3: [A] Message in A envelope
     *     Thread3: [B] Message in B envelope
     *     Thread3: [C] Message in C envelope
     */


        return 0;
    }
    
}
