cmake_minimum_required (VERSION 3.16)
project (http-client CXX)

include (${CMAKE_INSTALL_PREFIX}/lib/pstore/pstore.cmake)

set (client_root "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory (lib)

function (client_add_executable)
    cmake_parse_arguments (arg ""
        "NAME"
        "SOURCES"
        ${ARGN}
        )

    if ("${arg_NAME}" STREQUAL "")
        message (SEND_ERROR "add_test_executable: NAME argument is empty")
    endif ()
    if ("${arg_SOURCES}" STREQUAL "")
        message (SEND_ERROR "add_test_executable: no SOURCES were supplied")
    endif ()

    set (target ${arg_NAME})
    add_executable (${target} ${arg_SOURCES})
    target_link_libraries (${target} PUBLIC pstore::pstore-http)
    target_link_libraries (${target} PUBLIC client)

    set_target_properties (${target} PROPERTIES
        CXX_STANDARD 14
        CXX_STANDARD_REQUIRED Yes
        )

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options (${target} PRIVATE
            -Weverything
            -Wno-c++98-compat
            -Wno-c++98-compat-pedantic
            -Wno-exit-time-destructors
            -Wno-nullability-extension
            -Wno-padded
            -Wno-poison-system-directories
            -Wno-unused-lambda-capture
            )
     elseif (CMAKE_COMPILER_IS_GNUCXX)
        target_compile_options (${target} PRIVATE
            -Wall
            -Wextra
            -pedantic
        )
    elseif (MSVC)
        target_compile_options (${target} PRIVATE -W4)
    endif ()

endfunction (client_add_executable)


client_add_executable (NAME get SOURCES get.cpp)
client_add_executable (NAME ws SOURCES ws.cpp)
