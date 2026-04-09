# n02 - Open Kaillera Client static library
# Compiled as a static library linked into RMG-Core

add_library(n02 STATIC
    # Main client
    n02_client.cpp
    n02_callbacks.cpp

    # Error handling / logging
    errr.cpp

    # Statistics
    stats.cpp

    # Common utilities
    common/nThread.cpp
    common/nSettings.cpp
    common/k_socket.cpp

    # P2P protocol core
    core/p2p_core.cpp
    core/p2p_instruction.cpp

    # Kaillera protocol core
    kcore/kaillera_core.cpp
    kcore/k_instruction.cpp
)

target_include_directories(n02 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(n02 PRIVATE cxx_std_20)

# Link WinSock2 on Windows
if(WIN32)
    target_link_libraries(n02 PRIVATE ws2_32)
endif()

# Suppress warnings for legacy C-style code
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(n02 PRIVATE
        -Wno-write-strings
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-sign-compare
        -Wno-narrowing
        -Wno-format
        -Wno-deprecated-declarations
    )
endif()
