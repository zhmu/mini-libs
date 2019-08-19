cmake_minimum_required(VERSION 3.13)
project(mini-libs)

add_definitions(-std=c++17)

option(INSTALL_GTEST FALSE)
add_subdirectory(third-party/googletest)

include_directories(include)
add_subdirectory(test)
