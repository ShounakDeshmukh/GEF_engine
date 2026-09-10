add_executable(server server.cpp)
target_link_libraries(server PRIVATE engine)

add_executable(client client.cpp)
target_link_libraries(client PRIVATE engine)
