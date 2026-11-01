cmake_minimum_required(VERSION 3.22)

project(VstPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    message(FATAL_ERROR "vst-player requires MSVC (Visual Studio 2022 Build Tools). Run tools/build.ps1.")
endif()

add_executable(VstPlayer src/Main.cpp)
target_compile_options(VstPlayer PRIVATE /W4 /permissive- /utf-8)
