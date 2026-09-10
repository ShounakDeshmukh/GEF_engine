#include <engine/engine.hpp>
#include <cstring>
#include <thread>

int main(int argv, char* argc[]) {

    if(argv > 1 && !strcmp(argc[1], "subpub"))
    {
        auto pub = engine::publisher("tcp://*:5555");


        pub.publish("test message 1");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        pub.publish("Hello world");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        pub.publish("rule of threes");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));



        pub.publish();
    }
    else
    {
        std::function<std::string(std::string)> func = [](std::string input){
            if(!input.compare("MAGIC STRING"))
            {
                return std::string("YOU FOUND THE MAGIC STRING");
            }
            return std::string("Hello Client: you sent") + input;
        };
        
        auto resHand = engine::responseHandler("tcp://*:5555");
    
        resHand.run(func);

    }

}