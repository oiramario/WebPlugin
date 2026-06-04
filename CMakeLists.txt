cmake_minimum_required(VERSION 3.10)

# -- Project ----------------------------------------------------------
# WebP image decoder plug-in for ACDSee Pro 5 (32-bit).
# Build artifact: ID_WebP.apl (a DLL with .apl extension).
project(WebPlugin VERSION 1.0.38 LANGUAGES CXX)

# -- Global settings --------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    add_compile_options(/source-charset:utf-8)
endif()

# -- Options ----------------------------------------------------------
option(DEBUG_WEBP_TRACE "Enable WebP trace logging via OutputDebugString" OFF)

# -- Sub-projects -----------------------------------------------------
add_subdirectory(codec)   # WebPCodec (static) + vendored libwebp
add_subdirectory(plugin)  # ID_WebP.apl   (ACDSee plug-in)
