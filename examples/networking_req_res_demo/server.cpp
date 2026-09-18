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



        // std::function<std::string(std::string)> func = [](std::string input){
        //     if(!input.compare("MAGIC STRING"))
        //     {
        //         return std::string("YOU FOUND THE MAGIC STRING");
        //     }
        //     return std::string("Hello Client: you sent") + input;
        // };
        
        auto resHand = engine::networking::responseHandler("tcp://*:5555");
    
        resHand.run<engine::Transform,engine::Transform>([](engine::Transform input){
            input.position.x += 5;
            input.position.y += 10;
            return input;
        });

    }

}