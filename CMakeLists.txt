cmake_minimum_required(VERSION 3.24)
project(prism LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 REQUIRED CONFIG)

# WIN32: a GUI app on Windows (no console window); SDL_main.h supplies WinMain.
add_executable(prism WIN32 src/main.cpp src/audio.cpp)
target_link_libraries(prism PRIVATE SDL3::SDL3)

if(MSVC)
  target_compile_options(prism PRIVATE /W3 /utf-8)
  target_compile_definitions(prism PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(prism PRIVATE -Wall -Wextra)
endif()
