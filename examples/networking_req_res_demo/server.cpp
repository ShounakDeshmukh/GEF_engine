#include <engine/engine.hpp>



int main() {

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