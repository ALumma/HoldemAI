cmake_minimum_required(VERSION 3.20)
project(HoldemAI VERSION 0.1.0 LANGUAGES CXX)

# Interface-only until a concrete rules implementation is added.
add_library(holdem_engine INTERFACE)
target_include_directories(holdem_engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(holdem_engine INTERFACE cxx_std_20)

add_executable(engine_outline examples/engine_outline.cpp)
target_link_libraries(engine_outline PRIVATE holdem_engine)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(engine_outline PRIVATE -Wall -Wextra -Wpedantic)
endif()
