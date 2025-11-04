cmake_minimum_required(VERSION 3.18)

project(cs744_server LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

find_package(httplib CONFIG REQUIRED)
find_package(libpqxx CONFIG REQUIRED)

add_executable(server_app server.cpp)

target_link_libraries(server_app PRIVATE
    httplib::httplib
    libpqxx::pqxx
)