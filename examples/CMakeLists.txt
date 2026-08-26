add_executable(hello_window hello_window/main.cpp)
target_link_libraries(hello_window PRIVATE engine)

add_executable(hello_entity hello_entity/main.cpp)
target_link_libraries(hello_entity PRIVATE engine)
