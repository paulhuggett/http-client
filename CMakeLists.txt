cmake_minimum_required (VERSION 3.17)
project (http-client CXX)

include (${CMAKE_INSTALL_PREFIX}/lib/pstore/pstore.cmake)

set (client_root "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory (lib)

add_executable (get get.cpp)
target_link_libraries (get PUBLIC pstore::pstore-http)
target_link_libraries (get PUBLIC client)

set_target_properties (get PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED Yes
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options (get PRIVATE
        -Weverything
        -Wno-c++98-compat
        -Wno-c++98-compat-pedantic
        -Wno-exit-time-destructors
        -Wno-nullability-extension
        -Wno-padded
        -Wno-poison-system-directories
        -Wno-unused-lambda-capture
    )
endif ()
