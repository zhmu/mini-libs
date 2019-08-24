include_directories(${CMAKE_SOURCE_DIR}/third-party/googletest/googletest/include)

add_executable(test_deflate deflate.cpp)
target_link_libraries(test_deflate gtest_main)
add_executable(test_adler32 adler32.cpp)
target_link_libraries(test_adler32 gtest_main)
