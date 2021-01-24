cmake_minimum_required (VERSION 3.19)
project (http-client)

find_library (PSTORE_HTTP    pstore-http    PATHS /usr/local/lib/pstore)
find_library (PSTORE_OS      pstore-os      PATHS /usr/local/lib/pstore)
find_library (PSTORE_SUPPORT pstore-support PATHS /usr/local/lib/pstore)

add_executable (http-client http.cpp)
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
set_target_properties (http-client PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED Yes)
target_link_libraries (http-client PRIVATE ${PSTORE_HTTP} ${PSTORE_OS} ${PSTORE_SUPPORT})
target_include_directories (http-client PRIVATE /usr/local/include)
