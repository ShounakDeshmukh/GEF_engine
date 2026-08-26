add_executable(hello_window hello_window/main.cpp)
target_link_libraries(hello_window PRIVATE engine)

add_executable(hello_entity hello_entity/main.cpp)
target_link_libraries(hello_entity PRIVATE engine)
add_custom_command(TARGET hello_entity POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/hello_entity/assets
        $<TARGET_FILE_DIR:hello_entity>/assets
)
