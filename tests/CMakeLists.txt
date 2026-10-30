# Headless tests for the corruption engine (corrupt/). No emulator, no Qt:
# the engine talks to memory through MemoryAccess, which the tests fake.
cmake_minimum_required(VERSION 3.20)
project(corrupt-tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Everything in corrupt/ except the glue to emulator globals.
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS ../corrupt/*.cpp)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "(core_memory|core_access|hook)\\.cpp$")
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS corrupt/*.cpp)

add_executable(corrupt-tests ${ENGINE_SOURCES} ${TEST_SOURCES})
target_include_directories(corrupt-tests PRIVATE .. ../external)
target_compile_definitions(corrupt-tests PRIVATE CORRUPT_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../docs/corruption/examples")
if(NOT MSVC)
    target_compile_options(corrupt-tests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME corrupt-tests COMMAND corrupt-tests)

# Headless tests against the real emulator core (needs zlib + libpng).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ZLIB QUIET zlib)
    pkg_check_modules(PNG QUIET libpng)
endif()

if(ZLIB_FOUND AND PNG_FOUND)
    include(../cmake/snes9x_core.cmake)
    target_compile_definitions(snes9x-core PRIVATE ZLIB HAVE_STDINT_H HAVE_LIBPNG ALLOW_CPU_OVERCLOCK)
    target_include_directories(snes9x-core PRIVATE ${PNG_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})

    add_library(snes9x-headless STATIC headless/headless_core.cpp headless/headless_platform.cpp)
    target_include_directories(snes9x-headless PUBLIC .. ../external headless ${PNG_INCLUDE_DIRS})
    target_compile_definitions(snes9x-headless PUBLIC ZLIB HAVE_STDINT_H HAVE_LIBPNG ALLOW_CPU_OVERCLOCK)
    target_link_libraries(snes9x-headless PUBLIC snes9x-core ${PNG_LINK_LIBRARIES} ${ZLIB_LINK_LIBRARIES})

    add_executable(corrupt-smoke headless/corrupt_smoke.cpp)
    target_link_libraries(corrupt-smoke PRIVATE snes9x-headless)

    add_executable(corrupt-headless-tests headless/guard_integration_test.cpp headless/test_rom.cpp)
    target_link_libraries(corrupt-headless-tests PRIVATE snes9x-headless)
    add_test(NAME corrupt-headless-tests COMMAND corrupt-headless-tests)
else()
    message(STATUS "zlib/libpng not found: skipping the headless core tests")
endif()

# Qt widget tests for the corruption panel, run offscreen (needs Qt 6).
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    add_executable(corrupt-qt-tests qt/main.cpp qt/rule_editor_test.cpp qt/intensity_slider_test.cpp qt/memory_view_test.cpp qt/memory_viewer_test.cpp ../qt/src/RuleEditor.cpp ../qt/src/IntensitySlider.cpp ../qt/src/MemoryView.cpp ../qt/src/MemoryViewer.cpp ${ENGINE_SOURCES})
    set_target_properties(corrupt-qt-tests PROPERTIES AUTOMOC ON)
    target_include_directories(corrupt-qt-tests PRIVATE .. ../external ../qt/src)
    target_link_libraries(corrupt-qt-tests PRIVATE Qt6::Widgets)
    add_test(NAME corrupt-qt-tests COMMAND corrupt-qt-tests)
else()
    message(STATUS "Qt 6 not found: skipping the panel widget tests")
endif()
