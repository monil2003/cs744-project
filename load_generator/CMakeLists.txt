cmake_minimum_required(VERSION 3.18)

project(load_generator LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

find_package(httplib CONFIG REQUIRED)

add_executable(loader main.cpp)

target_link_libraries(loader PRIVATE
    httplib::httplib
    Threads::Threads
)