cmake_minimum_required(VERSION 3.16)
project(LightningXML
    VERSION 1.3.0
    LANGUAGES CXX
    DESCRIPTION "High-performance XML pull-parser for C++20"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
message(STATUS "LightningXML: Build Type = ${CMAKE_BUILD_TYPE}.")

# ---- Options ----
option(LIGHTNINGXML_BUILD_TESTS        "Build unit tests (requires GTest)"                      ON)
option(LIGHTNINGXML_BUILD_BENCHMARKS   "Build benchmarks (requires Google Benchmark)"           ON)
option(LIGHTNINGXML_BUILD_CODEGEN      "Build the XSD -> XmlMetadata code generator (xsdgen)"   ON)
option(LIGHTNINGXML_WITH_PUGIXML       "Build pugixml comparison benchmarks"                    OFF)
option(LIGHTNINGXML_WITH_RAPIDXML      "Build RapidXML comparison benchmarks (via Boost)"       OFF)
option(LIGHTNINGXML_WITH_LIBXML2       "Build libxml2 comparison benchmarks"                    OFF)
option(LIGHTNINGXML_BUILD_FUZZERS      "Build libFuzzer targets (requires Clang)"               OFF)
option(LIGHTNINGXML_ENABLE_SANITIZERS  "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LIGHTNINGXML_ENABLE_COVERAGE    "Enable gcov coverage instrumentation"                   OFF)
option(LIGHTNINGXML_ENABLE_UNITY_BUILD "Compile executables as unity builds"                    ON)
option(LIGHTNINGXML_ENABLE_LTO         "Enable link-time optimization (IPO) for executables"    ON)

# Coverage wants unoptimized, non-LTO
if(LIGHTNINGXML_ENABLE_COVERAGE)
    message(STATUS "LightningXML: coverage instrumentation enabled")
    set(LIGHTNINGXML_ENABLE_UNITY_BUILD OFF)
    set(LIGHTNINGXML_ENABLE_LTO OFF)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(--coverage)
        add_link_options(--coverage)
    else()
        message(FATAL_ERROR
            "Coverage is only supported for GCC/Clang"
        )
    endif()
endif()

if(LIGHTNINGXML_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_error)
    if(NOT _ipo_supported)
        message(FATAL_ERROR "LTO requested but not supported: ${_ipo_error}")
    endif()
    message(STATUS "LightningXML: LTO enabled")
endif()

if(LIGHTNINGXML_ENABLE_UNITY_BUILD)
    message(STATUS "LightningXML: unity build enabled")
endif()

# Applies the unity/LTO options to one of this project's executables (the
# options deliberately do not propagate into FetchContent dependencies).
function(lightningxml_build_options target)
    if(LIGHTNINGXML_ENABLE_UNITY_BUILD)
        set_target_properties(${target} PROPERTIES UNITY_BUILD ON)
    endif()
    if(LIGHTNINGXML_ENABLE_LTO)
        set_target_properties(${target} PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
endfunction()

if(LIGHTNINGXML_ENABLE_SANITIZERS)
    message(STATUS "LightningXML: sanitizers enabled")

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )

        add_link_options(
            -fsanitize=address,undefined
        )
    else()
        message(FATAL_ERROR
            "Sanitizers are only supported for GCC/Clang"
        )
    endif()
endif()

# ---- Header-only library target ----
add_library(lightningxml INTERFACE)
add_library(LightningXML::lightningxml ALIAS lightningxml)

target_include_directories(lightningxml INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(lightningxml INTERFACE cxx_std_20)

# ---- Shared test helpers (used by both tests and benchmarks) ----
add_library(lightningxml_helpers INTERFACE)
target_include_directories(lightningxml_helpers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/test
)
target_link_libraries(lightningxml_helpers INTERFACE lightningxml)

# ---- Dependencies via FetchContent ----
include(FetchContent)

# ---- Tests ----
if(LIGHTNINGXML_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        FIND_PACKAGE_ARGS NAMES GTest
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(lightningxml_tests
        test/test_LightningXML.cc
        test/test_Conformance.cc
        test/test_XSDCodegen.cc
    )
    lightningxml_build_options(lightningxml_tests)
    target_include_directories(lightningxml_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools
    )
    target_link_libraries(lightningxml_tests PRIVATE
        lightningxml_helpers
        GTest::gtest_main
    )
    target_compile_options(lightningxml_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    )

    include(GoogleTest)
    gtest_discover_tests(lightningxml_tests)

    message(STATUS "LightningXML: tests enabled")
endif()

# ---- Fuzzers ----
# libFuzzer ships with Clang only. The sanitizers are part of the target rather
# than LIGHTNINGXML_ENABLE_SANITIZERS because a fuzzer without them reports
# almost nothing: the crashes worth finding here are reads off the end of a
# buffer, not assertion failures.
if(LIGHTNINGXML_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "LIGHTNINGXML_BUILD_FUZZERS requires Clang (libFuzzer)")
    endif()

    add_executable(lightningxml_fuzz test/fuzz_LightningXML.cc)
    target_link_libraries(lightningxml_fuzz PRIVATE lightningxml)
    target_compile_options(lightningxml_fuzz PRIVATE
        -Wall -Wextra -Wpedantic
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(lightningxml_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)

    message(STATUS "LightningXML: fuzzers enabled")
endif()

# ---- Benchmarks ----
if(LIGHTNINGXML_BUILD_BENCHMARKS)
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
    if(LIGHTNINGXML_WITH_PUGIXML)
        FetchContent_Declare(
            pugixml
            GIT_REPOSITORY https://github.com/zeux/pugixml.git
            GIT_TAG        v1.14
            FIND_PACKAGE_ARGS NAMES pugixml
        )
        FetchContent_MakeAvailable(pugixml)
        message(STATUS "LightningXML: pugixml comparison benchmarks enabled")
    endif()

    # RapidXML (optional). RapidXML has no canonical upstream package, so we use
    # the copy bundled in Boost.PropertyTree (boost::property_tree::detail::rapidxml).
    # The official Boost "-cmake" release archive ships every header with no git
    # submodules; BOOST_INCLUDE_LIBRARIES restricts configuration to the
    # property_tree subtree (and its dependencies) so the first build stays quick.
    if(LIGHTNINGXML_WITH_RAPIDXML)
        set(BOOST_INCLUDE_LIBRARIES property_tree CACHE STRING "" FORCE)
        set(BOOST_ENABLE_CMAKE ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            Boost
            URL https://github.com/boostorg/boost/releases/download/boost-1.88.0/boost-1.88.0-cmake.tar.xz
        )
        FetchContent_MakeAvailable(Boost)
        message(STATUS "LightningXML: RapidXML (Boost) comparison benchmarks enabled")
    endif()

    # libxml2 (optional). A feature-rich parser: copies all strings, decodes
    # entities, and fully validates well-formedness. Fetched and built from
    # source with the optional codecs/tools disabled to keep the build lean.
    if(LIGHTNINGXML_WITH_LIBXML2)
        set(LIBXML2_WITH_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(LIBXML2_WITH_TESTS    OFF CACHE BOOL "" FORCE)
        set(LIBXML2_WITH_PYTHON   OFF CACHE BOOL "" FORCE)
        set(LIBXML2_WITH_ICONV    OFF CACHE BOOL "" FORCE)
        set(LIBXML2_WITH_ZLIB     OFF CACHE BOOL "" FORCE)
        set(LIBXML2_WITH_LZMA     OFF CACHE BOOL "" FORCE)
        set(LIBXML2_WITH_HTTP     OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            libxml2
            GIT_REPOSITORY https://gitlab.gnome.org/GNOME/libxml2.git
            GIT_TAG        v2.14.6
        )
        FetchContent_MakeAvailable(libxml2)
        message(STATUS "LightningXML: libxml2 comparison benchmarks enabled")
    endif()

    # --- Timing benchmarks ---
    add_executable(lightningxml_bench test/bench_LightningXML.cc)
    lightningxml_build_options(lightningxml_bench)
    target_link_libraries(lightningxml_bench PRIVATE lightningxml_helpers benchmark::benchmark)
    target_compile_options(lightningxml_bench PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    )
    if(LIGHTNINGXML_WITH_PUGIXML)
        target_link_libraries(lightningxml_bench PRIVATE pugixml::pugixml)
        target_compile_definitions(lightningxml_bench PRIVATE LIGHTNINGXML_HAS_PUGIXML)
    endif()
    if(LIGHTNINGXML_WITH_RAPIDXML)
        target_link_libraries(lightningxml_bench PRIVATE Boost::property_tree)
        target_compile_definitions(lightningxml_bench PRIVATE LIGHTNINGXML_HAS_RAPIDXML)
    endif()
    if(LIGHTNINGXML_WITH_LIBXML2)
        target_link_libraries(lightningxml_bench PRIVATE LibXml2::LibXml2)
        target_compile_definitions(lightningxml_bench PRIVATE LIGHTNINGXML_HAS_LIBXML2)
    endif()
    message(STATUS "LightningXML: benchmarks enabled")
endif()

# ---- XSD code generator ----
if(LIGHTNINGXML_BUILD_CODEGEN)
    add_executable(lightningxml_xsdgen tools/XSDGen.cc)
    lightningxml_build_options(lightningxml_xsdgen)
    target_link_libraries(lightningxml_xsdgen PRIVATE lightningxml)
    target_include_directories(lightningxml_xsdgen PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools
    )
    target_compile_options(lightningxml_xsdgen PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    )
    message(STATUS "LightningXML: XSD code generator enabled")
endif()

# ---- Generated-code test: xsdgen output must compile and round-trip ----
if(LIGHTNINGXML_BUILD_TESTS AND LIGHTNINGXML_BUILD_CODEGEN)
    set(_gen_hh ${CMAKE_CURRENT_BINARY_DIR}/generated/kitchensink_generated.hh)
    add_custom_command(
        OUTPUT ${_gen_hh}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
        COMMAND lightningxml_xsdgen ${CMAKE_CURRENT_SOURCE_DIR}/test/schemas/kitchensink.xsd -o ${_gen_hh}
        DEPENDS lightningxml_xsdgen
                ${CMAKE_CURRENT_SOURCE_DIR}/test/schemas/kitchensink.xsd
                ${CMAKE_CURRENT_SOURCE_DIR}/test/schemas/common.xsd
                ${CMAKE_CURRENT_SOURCE_DIR}/test/schemas/base.xsd
        COMMENT "Generating kitchensink_generated.hh from XSD"
    )
    target_sources(lightningxml_tests PRIVATE test/test_Generated.cc ${_gen_hh})
    target_include_directories(lightningxml_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
endif()

# ---- Installation & package config ----
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS lightningxml EXPORT LightningXMLTargets)
install(FILES include/LightningXML.hh DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT LightningXMLTargets
    FILE LightningXMLConfig.cmake
    NAMESPACE LightningXML::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LightningXML
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/LightningXMLConfigVersion.cmake
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/LightningXMLConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LightningXML
)
if(TARGET lightningxml_xsdgen)
    install(TARGETS lightningxml_xsdgen DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# Developer convenience: copy the test/bench/codegen binaries into <source>/bin.
set(INSTALL_DIR "${CMAKE_SOURCE_DIR}/bin")
foreach(_target lightningxml_tests lightningxml_bench lightningxml_xsdgen)
    if(TARGET ${_target})
        install(TARGETS ${_target} DESTINATION ${INSTALL_DIR})
    endif()
endforeach()
message(STATUS "Installing binaries to ${INSTALL_DIR}")