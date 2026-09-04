add_executable(hello_window hello_window/main.cpp)
target_link_libraries(hello_window PRIVATE engine)

add_executable(hello_entity hello_entity/main.cpp)
target_link_libraries(hello_entity PRIVATE engine)
target_compile_definitions(hello_entity PRIVATE
    HELLO_ENTITY_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/hello_entity/assets/"
)

add_executable(gravity_demo gravity_demo/main.cpp)
target_link_libraries(gravity_demo PRIVATE engine)
target_compile_definitions(gravity_demo PRIVATE
    GRAVITY_DEMO_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/hello_entity/assets/"
)

add_executable(collision_demo collision_demo/main.cpp)
target_link_libraries(collision_demo PRIVATE engine)

add_executable(scaling_demo scaling_demo/main.cpp)
target_link_libraries(scaling_demo PRIVATE engine)
