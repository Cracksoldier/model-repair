add_library(modelrepair_warnings INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(modelrepair_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    )
    if(MODELREPAIR_ENABLE_ASAN)
        target_compile_options(modelrepair_warnings INTERFACE -fsanitize=address,undefined)
        target_link_options(modelrepair_warnings INTERFACE    -fsanitize=address,undefined)
    endif()
endif()
