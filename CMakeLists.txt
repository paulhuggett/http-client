cmake_minimum_required (VERSION 3.19)
project (httpclient)

#CXXFLAGS = \
#	-Weverything                   \
#	-Wno-c++98-compat              \
#	-Wno-c++98-compat-pedantic     \
#	-Wno-nullability-extension     \
#	-Wno-padded                    \
#	-Wno-poison-system-directories \
#	-Wno-unused-lambda-capture     \

find_library (PSTORE_HTTP    pstore-http    PATHS /usr/local/lib/pstore)
find_library (PSTORE_OS      pstore-os      PATHS /usr/local/lib/pstore)
find_library (PSTORE_SUPPORT pstore-support PATHS /usr/local/lib/pstore)

add_executable (httpclient http.cpp)
target_compile_options (httpclient PRIVATE
    -Weverything
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
    -Wno-nullability-extension
    -Wno-padded
    -Wno-poison-system-directories
    -Wno-unused-lambda-capture
    )

set_target_properties (httpclient PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED Yes)
target_link_libraries (httpclient PRIVATE ${PSTORE_HTTP} ${PSTORE_OS} ${PSTORE_SUPPORT})
target_include_directories (httpclient PRIVATE /usr/local/include)
