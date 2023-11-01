add_library (client STATIC
    client.cpp
    "${client_root}/include/client/client.hpp"
)
target_include_directories (client PUBLIC "${client_root}/include")
set_target_properties (client PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED Yes
)
target_link_libraries (client PUBLIC pstore::pstore-http)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options (client PRIVATE
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

    
