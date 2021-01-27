cmake_minimum_required (VERSION 3.17)
project (http-client CXX)

include (${CMAKE_INSTALL_PREFIX}/lib/pstore/pstore.cmake)

add_executable (http-client main.cpp)
target_link_libraries (http-client PUBLIC pstore::pstore-http)
set_target_properties (http-client PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED Yes
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options (http-client PRIVATE
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
