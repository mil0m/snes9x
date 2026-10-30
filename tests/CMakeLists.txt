# Headless tests for the corruption engine (corrupt/). No emulator, no Qt:
# the engine talks to memory through MemoryAccess, which the tests fake.
cmake_minimum_required(VERSION 3.20)
project(corrupt-tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Everything in corrupt/ except the glue to emulator globals.
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS ../corrupt/*.cpp)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "(core_memory|hook)\\.cpp$")
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS corrupt/*.cpp)

add_executable(corrupt-tests ${ENGINE_SOURCES} ${TEST_SOURCES})
target_include_directories(corrupt-tests PRIVATE .. ../external)
if(NOT MSVC)
    target_compile_options(corrupt-tests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME corrupt-tests COMMAND corrupt-tests)
