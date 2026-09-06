cmake_minimum_required(VERSION 3.28)
project(engine VERSION 0.1.0 LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
            Debug Release RelWithDebInfo MinSizeRel)
    endif()
elseif(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    # CMAKE_BUILD_TYPE is a whole-tree cache variable, so setting it here would
    # silently pick the build type for the consuming project's own targets too.
    # Warn instead: with no build type, SDL3 and its vendored deps compile at -O0.
    message(WARNING
        "engine: CMAKE_BUILD_TYPE is empty, so this build gets no optimization "
        "flags at all - SDL3, freetype and harfbuzz will compile at -O0. The "
        "engine will not set it for you, because it would override your own "
        "targets. Re-run cmake with -DCMAKE_BUILD_TYPE=Debug, RelWithDebInfo, "
        "or Release.")
endif()

option(ENGINE_BUILD_TESTS "Build the engine test suite" ${PROJECT_IS_TOP_LEVEL})
option(ENGINE_BUILD_EXAMPLES "Build the engine example programs" ${PROJECT_IS_TOP_LEVEL})

include(cmake/Dependencies.cmake)

add_library(engine STATIC
    src/window.cpp
    src/renderer.cpp
    src/log.cpp
    src/entity.cpp
    src/timeline.cpp
    src/animation.cpp
    src/physics.cpp
    src/inputHandler.cpp
)
add_library(engine::engine ALIAS engine)

target_compile_features(engine PUBLIC cxx_std_20)
set_target_properties(engine PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(engine
    PUBLIC
        glm::glm
        fmt::fmt
    PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        spdlog::spdlog
)

target_compile_options(engine PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra>
)

if(ENGINE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ENGINE_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()

if(PROJECT_IS_TOP_LEVEL)
    find_program(CLANG_FORMAT_EXE NAMES clang-format)
    if(CLANG_FORMAT_EXE)
        file(GLOB_RECURSE ENGINE_FORMAT_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
        )
        add_custom_target(format
            COMMAND ${CLANG_FORMAT_EXE} -i ${ENGINE_FORMAT_SOURCES}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Formatting all engine sources with clang-format"
            VERBATIM
        )
    endif()

    find_program(DOXYGEN_EXE NAMES doxygen)
    if(DOXYGEN_EXE)
        add_custom_target(docs
            COMMAND ${DOXYGEN_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
        )
    endif()
endif()
