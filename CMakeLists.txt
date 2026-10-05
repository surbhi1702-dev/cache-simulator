cmake_minimum_required(VERSION 3.16)
project(cachesim VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CACHESIM_BUILD_TESTS "Build unit tests" ON)
option(CACHESIM_SANITIZE "Build with AddressSanitizer + UBSan" OFF)

add_library(cachesim_core
  src/types.cpp
  src/replacement_policy.cpp
  src/memory_level.cpp
  src/config.cpp
  src/cache.cpp
  src/hierarchy.cpp
  src/trace.cpp
  src/report.cpp
)
target_include_directories(cachesim_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
  target_compile_options(cachesim_core PRIVATE /W4)
else()
  target_compile_options(cachesim_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

if(CACHESIM_SANITIZE AND NOT MSVC)
  target_compile_options(cachesim_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(cachesim_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(cachesim src/main.cpp)
target_link_libraries(cachesim PRIVATE cachesim_core)

if(CACHESIM_BUILD_TESTS)
  enable_testing()
  add_executable(cachesim_tests
    tests/test_main.cpp
    tests/test_config.cpp
    tests/test_replacement.cpp
    tests/test_cache.cpp
    tests/test_hierarchy.cpp
    tests/test_trace.cpp
  )
  target_link_libraries(cachesim_tests PRIVATE cachesim_core)
  target_compile_definitions(cachesim_tests PRIVATE CACHESIM_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/traces")
  add_test(NAME unit_tests COMMAND cachesim_tests)

  # End-to-end smoke tests of the CLI.
  add_test(NAME cli_help COMMAND cachesim --help)
  add_test(NAME cli_json COMMAND cachesim --pattern random --accesses 5000 --format json)
  add_test(NAME cli_trace_file COMMAND cachesim --trace ${CMAKE_CURRENT_SOURCE_DIR}/traces/matrix_8x8.trace
           --level name=L1,size=256,assoc=2,block=16 --format csv)
  add_test(NAME cli_config_file COMMAND cachesim --config ${CMAKE_CURRENT_SOURCE_DIR}/configs/default.cfg --accesses 5000)
  add_test(NAME cli_rejects_bad_geometry COMMAND cachesim --level size=3000,block=64)
  set_tests_properties(cli_rejects_bad_geometry PROPERTIES WILL_FAIL TRUE)
endif()
