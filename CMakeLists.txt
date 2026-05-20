cmake_minimum_required(VERSION 3.10)

project(WebPlugin VERSION 1.0.37 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(MSVC)
    add_compile_options(/source-charset:utf-8)
endif()

add_subdirectory(codec)
add_subdirectory(plugin)
add_subdirectory(test)
