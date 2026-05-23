cmake_minimum_required(VERSION 3.14)
project(smoke LANGUAGES C CXX)

add_subdirectory(../../ ${CMAKE_BINARY_DIR}/breakpad-lib EXCLUDE_FROM_ALL)

add_executable(smoke_app main.cpp)
target_link_libraries(smoke_app PRIVATE breakpad::breakpad)

enable_testing()
add_test(NAME integration_test COMMAND smoke_app)
