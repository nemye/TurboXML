cmake_minimum_required(VERSION 3.16)
project(TurboXML
    VERSION 1.1.0
    LANGUAGES CXX
    DESCRIPTION "High-performance XML pull-parser for C++20"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "TurboXML: Build Type = ${CMAKE_BUILD_TYPE}.")

# ---- Options ----
option(TURBOXML_BUILD_TESTS      "Build unit tests (requires GTest)"           ON)
option(TURBOXML_BUILD_BENCHMARKS "Build benchmarks (requires Google Benchmark)" ON)
option(TURBOXML_WITH_PUGIXML     "Build pugixml comparison benchmarks"          OFF)

# ---- Header-only library target ----
add_library(turboxml INTERFACE)
add_library(TurboXML::turboxml ALIAS turboxml)

target_include_directories(turboxml INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(turboxml INTERFACE cxx_std_20)

# ---- Shared test helpers (used by both tests and benchmarks) ----
add_library(turboxml_helpers INTERFACE)
target_include_directories(turboxml_helpers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/test
)
target_link_libraries(turboxml_helpers INTERFACE turboxml)

# ---- Dependencies via FetchContent ----
include(FetchContent)

# ---- Tests ----
if(TURBOXML_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        FIND_PACKAGE_ARGS NAMES GTest
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(turboxml_tests
        test/test_TurboXML.cc
    )
    target_link_libraries(turboxml_tests PRIVATE
        turboxml_helpers
        GTest::gtest_main
    )
    target_compile_options(turboxml_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    )

    include(GoogleTest)
    gtest_discover_tests(turboxml_tests)

    message(STATUS "TurboXML: tests enabled")
endif()

# ---- Benchmarks ----
if(TURBOXML_BUILD_BENCHMARKS)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.1
        FIND_PACKAGE_ARGS NAMES benchmark
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)

    # pugixml (optional)
    if(TURBOXML_WITH_PUGIXML)
        FetchContent_Declare(
            pugixml
            GIT_REPOSITORY https://github.com/zeux/pugixml.git
            GIT_TAG        v1.14
            FIND_PACKAGE_ARGS NAMES pugixml
        )
        FetchContent_MakeAvailable(pugixml)
        message(STATUS "TurboXML: pugixml comparison benchmarks enabled")
    endif()

    # --- Timing benchmarks ---
    add_executable(turboxml_bench test/bench_TurboXML.cc)
    target_link_libraries(turboxml_bench PRIVATE turboxml_helpers benchmark::benchmark)
    if(TURBOXML_WITH_PUGIXML)
        target_link_libraries(turboxml_bench PRIVATE pugixml::pugixml)
        target_compile_definitions(turboxml_bench PRIVATE TURBOXML_HAS_PUGIXML)
    endif()

    message(STATUS "TurboXML: benchmarks enabled")
endif()

# Installation
set(INSTALL_DIR "${CMAKE_SOURCE_DIR}/bin")
foreach(_target turboxml_tests turboxml_bench)
    if(TARGET ${_target})
        install(TARGETS ${_target} DESTINATION ${INSTALL_DIR})
    endif()
endforeach()
message(STATUS "Installing binaries to ${INSTALL_DIR}")