add_executable(test_deflate deflate.cpp)
include_directories(${CMAKE_SOURCE_DIR}/third-party/googletest/googletest/include)
target_link_libraries(test_deflate gtest_main)
