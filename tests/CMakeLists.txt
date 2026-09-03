add_executable(engine_tests test_log.cpp test_entity.cpp test_physics.cpp test_input_handler.cpp)
target_link_libraries(engine_tests PRIVATE engine Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(engine_tests)
