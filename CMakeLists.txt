cmake_minimum_required(VERSION 3.20)
project(quake_pak_inspector LANGUAGES CXX)

add_executable(pak-inspector src/main.cpp src/pak.cpp)
target_compile_features(pak-inspector PRIVATE cxx_std_20)
set_target_properties(pak-inspector PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(pak-inspector PRIVATE /W4 /permissive- /utf-8)
  set_property(TARGET pak-inspector PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  target_compile_options(pak-inspector PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME cli
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.py"
            "$<TARGET_FILE:pak-inspector>")
endif()
