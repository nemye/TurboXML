# OMS UCI v2.5 add-on: generates the whole published message set with xsdgen and
# tests the result. See extras/uci/README.md.

set(UCI_COMMIT   "6d5af178bcd3b18b3a102bede101dea0af9a0ad5")
set(UCI_MESSAGES "UCI_MessageDefinitions_v2_5_0.xsd")
set(UCI_MARKINGS "UCI_SecurityMarkings_v2_5_0.xsd")
set(UCI_MESSAGES_SHA256 "ac9430499e1107371345e04430895c8c9f18578c1a6b022958ca43ae8aa7bf27")
set(UCI_MARKINGS_SHA256 "4a8056f1503234d423a0c2495844ab65387febced62bea58b87d4f343e9e7a0b")

# xsdgen resolves xs:include against the input file's directory, so both schemas
# have to sit side by side.
if(LIGHTNINGXML_UCI_SCHEMA_DIR)
    set(_uci_schema_dir "${LIGHTNINGXML_UCI_SCHEMA_DIR}")
    foreach(_file ${UCI_MESSAGES} ${UCI_MARKINGS})
        if(NOT EXISTS "${_uci_schema_dir}/${_file}")
            message(FATAL_ERROR
                "LIGHTNINGXML_UCI_SCHEMA_DIR=${_uci_schema_dir} has no ${_file}"
            )
        endif()
    endforeach()
    message(STATUS "LightningXML: UCI schema taken from ${_uci_schema_dir}")
else()
    set(_uci_schema_dir "${CMAKE_CURRENT_BINARY_DIR}/schema")
    set(_uci_base_url
        "https://raw.githubusercontent.com/open-arsenal/UCI/${UCI_COMMIT}/OAC-STD-UCI_V2.5"
    )
    # A matching EXPECTED_HASH makes this a no-op on reconfigure.
    function(uci_fetch file hash)
        file(DOWNLOAD
            "${_uci_base_url}/${file}"
            "${_uci_schema_dir}/${file}"
            EXPECTED_HASH SHA256=${hash}
            STATUS _status
            TLS_VERIFY ON
        )
        list(GET _status 0 _code)
        if(NOT _code EQUAL 0)
            list(GET _status 1 _message)
            message(FATAL_ERROR
                "Failed to download ${file}: ${_message}\n"
                "Point -DLIGHTNINGXML_UCI_SCHEMA_DIR=<dir> at a local copy of "
                "OAC-STD-UCI_V2.5 to build without network access."
            )
        endif()
    endfunction()
    uci_fetch(${UCI_MESSAGES} ${UCI_MESSAGES_SHA256})
    uci_fetch(${UCI_MARKINGS} ${UCI_MARKINGS_SHA256})
    message(STATUS "LightningXML: UCI schema fetched at ${UCI_COMMIT}")
endif()

set(_uci_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/uci_generated.hh")
add_custom_command(
    OUTPUT ${_uci_generated}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
    COMMAND lightningxml_xsdgen ${_uci_schema_dir}/${UCI_MESSAGES} -o ${_uci_generated}
    DEPENDS lightningxml_xsdgen
            ${_uci_schema_dir}/${UCI_MESSAGES}
            ${_uci_schema_dir}/${UCI_MARKINGS}
    COMMENT "Generating uci_generated.hh from UCI v2.5 (722 messages, 4612 types)"
)
add_custom_target(lightningxml_uci_header DEPENDS ${_uci_generated})

if(LIGHTNINGXML_BUILD_TESTS)
    # Deliberately not run through lightningxml_build_options(): the generated
    # header is a ~4 MB translation unit and LTO on top of it buys nothing.
    add_executable(lightningxml_uci_tests test_UCI.cc ${_uci_generated})
    target_include_directories(lightningxml_uci_tests PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
    target_link_libraries(lightningxml_uci_tests PRIVATE
        lightningxml
        GTest::gtest_main
    )
    target_compile_options(lightningxml_uci_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    )
    gtest_discover_tests(lightningxml_uci_tests)
endif()

message(STATUS "LightningXML: UCI add-on enabled")
