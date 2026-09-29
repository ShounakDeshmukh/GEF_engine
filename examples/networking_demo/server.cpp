#include <engine/engine.hpp>
#include <cstring>
#include <thread>

#include <engine/entity.hpp>

int main(int argv, char* argc[]) {

    if(argv > 1 && !strcmp(argc[1], "subpub"))
    {
        auto pub = engine::networking::publisher("tcp://*:5555");

        //gives time for subscriber to connect
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        engine::Transform boi; 
        boi.position.x = 23;
        boi.position.y = 43;

        while(true)
        {
            pub.publish(boi, "test/");
            pub.publish("pulse", "heartbeat/");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        }

    }
    else
    {

        auto resHand = engine::networking::responseHandler("tcp://*:5555");

        if(argv > 1 && !strcmp(argc[1], "string"))
        {
            resHand.run();
        }
        else if (argv > 1 && !strcmp(argc[1], "template"))
        {            
            resHand.run<engine::Transform,engine::Transform>([](engine::Transform input){
                input.position.x += 5;
                input.position.y += 10;
                return input;
            });

        }
        else if (argv > 1 && !strcmp(argc[1], "bytes"))
        {
            resHand.run<engine::networking::Bytes,engine::networking::Bytes>(
                [](engine::networking::Bytes& input){
                std::string requestString(reinterpret_cast<const char*>(input.data()), input.size());
                std::cout << "Received string as bytes: " << requestString << std::endl;
                std::string responseString = "Server received message as bytes: " + requestString;
                engine::networking::Bytes bytes(responseString.size());
                memcpy(bytes.data(), responseString.data(), responseString.size());
                return bytes;
            });
        }
        else
        {
            std::cout << "Usage: ./server (string/template/bytes/subpub)" << std::endl;
        }
        

    }

}