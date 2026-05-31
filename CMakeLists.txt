cmake_minimum_required(VERSION 3.10)

# Set modern CMake policies
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)  # MSVC runtime library flags
endif()
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)  # FetchContent timestamp
endif()

project(NetCopy CXX)

# Enable MASM assembler on MSVC (must be before any targets)
if(MSVC)
    enable_language(ASM_MASM)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Configure FetchContent to reuse downloads
set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)  # Don't update on every configure

FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-28-0
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(asio)


# CUDA configuration (optional)
option(ENABLE_CUDA "Enable CUDA support for GPU acceleration" OFF)
option(WITH_RIO "Compile experimental Windows Registered I/O capability hooks" OFF)
option(WITHRIO "Alias for WITH_RIO" OFF)
option(WITHIORING "Compile experimental Windows I/O Ring capability hooks" OFF)
option(WITH_MSQUIC "Compile experimental MsQuic capability hooks" OFF)
option(WITHMSQUIC "Alias for WITH_MSQUIC" OFF)
option(WITH_TCP_INFO_WINDOW "Use Windows SIO_TCP_INFO to tune transfer in-flight windows when requested by config" OFF)
option(WITHTCPINFO "Alias for WITH_TCP_INFO_WINDOW" OFF)

if(ENABLE_CUDA)
    # Set CUDA host compiler before enabling CUDA language
    if(MINGW)
        # For MinGW, we need to set the host compiler explicitly
        set(CMAKE_CUDA_HOST_COMPILER ${CMAKE_CXX_COMPILER})
        # Also set environment variables that NVCC uses
        set(ENV{CUDA_HOST_COMPILER} ${CMAKE_CXX_COMPILER})
        message(STATUS "Setting CUDA host compiler to: ${CMAKE_CXX_COMPILER}")
    endif()
    
    # Try modern CMake CUDA support first
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        enable_language(CUDA)
        set(CMAKE_CUDA_STANDARD 17)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)
        set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)
        
        # Set CUDA architectures for common GPUs including RTX 3080 Ti (Ampere - 8.6)
        set(CMAKE_CUDA_ARCHITECTURES "60;61;70;75;80;86")
        
        # CUDA compiler flags for MinGW compatibility
        set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --extended-lambda --expt-relaxed-constexpr")
        
        # MinGW specific CUDA settings
        if(MINGW)
            # Tell NVCC to use GCC as host compiler
            set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -ccbin=${CMAKE_CXX_COMPILER}")
            set(CMAKE_CUDA_HOST_COMPILER ${CMAKE_CXX_COMPILER})
        endif()
        
        message(STATUS "CUDA Toolkit found - GPU acceleration will be available")
        message(STATUS "CUDA Version: ${CUDAToolkit_VERSION}")
        add_definitions(-DCUDA_AVAILABLE)
        set(CUDA_ENABLED TRUE)
    else()
        # Fallback to legacy CUDA find module
        find_package(CUDA QUIET)
        if(CUDA_FOUND)
            enable_language(CUDA)
            set(CMAKE_CUDA_STANDARD 17)
            set(CMAKE_CUDA_STANDARD_REQUIRED ON)
            set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)
            set(CMAKE_CUDA_ARCHITECTURES "60;61;70;75;80;86")
            set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --extended-lambda --expt-relaxed-constexpr")
            
            if(MINGW)
                set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -ccbin=${CMAKE_CXX_COMPILER}")
                set(CMAKE_CUDA_HOST_COMPILER ${CMAKE_CXX_COMPILER})
            endif()
            
            message(STATUS "CUDA found (legacy) - GPU acceleration will be available")
            add_definitions(-DCUDA_AVAILABLE)
            set(CUDA_ENABLED TRUE)
        else()
            message(WARNING "CUDA requested but not found - GPU acceleration will not be available")
            message(STATUS "Make sure CUDA Toolkit is installed and CUDA_PATH is set")
            set(CUDA_ENABLED FALSE)
        endif()
    endif()
else()
    message(STATUS "CUDA support disabled - GPU acceleration will not be available")
    set(CUDA_ENABLED FALSE)
endif()

# Enable hardware acceleration flags for AES-NI and SIMD
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Add AES-NI and SIMD instruction set flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -maes -msse2 -mavx")
    
    # Optional: Add more advanced instruction sets if supported
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
    if(COMPILER_SUPPORTS_AVX2)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
    endif()
elseif(MSVC)
    # MSVC doesn't need explicit flags for AES-NI (enabled by default on x64)
    # But we can add /arch:AVX2 for better SIMD performance
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX2")
    add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    # Define Windows 10 target version to suppress ASIO warning
    add_definitions(-D_WIN32_WINNT=0x0A00)
    add_definitions(-DWINVER=0x0A00)
endif()

# Enable assembly support conditionally
if(MSVC)
    enable_language(ASM_MASM)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    enable_language(ASM)
endif()

# Common include directories
include_directories(include)

# Common source files
set(COMMON_SOURCES
    src/crypto/chacha20_poly1305.cpp
    src/crypto/xor_cipher.cpp
    src/crypto/aes_ctr.cpp
    src/crypto/crypto_engine.cpp
    src/crypto/sha3.cpp
    src/crypto/mlkem.cpp
    src/crypto/key_manager.cpp
    src/network/socket.cpp
    src/network/event_loop.cpp
    src/network/async_socket.cpp
    src/network/windows_experimental.cpp
    src/protocol/message.cpp
    src/file/file_manager.cpp
    src/config/config_parser.cpp
    src/logging/logger.cpp
    src/logging/audit_log.cpp
    src/common/utils.cpp
    src/common/bandwidth_monitor.cpp
    src/common/bandwidth_limiter.cpp
    src/common/chunk_size_manager.cpp
    src/common/compression.cpp
    src/auth/user_db.cpp
    src/auth/auth_engine.cpp
    src/common/fast_mem.cpp
    src/network/ssh_server.cpp
)

# Add conditional assembly files
if(MSVC)
    list(APPEND COMMON_SOURCES src/common/fast_memcpy_masm.asm)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    list(APPEND COMMON_SOURCES src/common/fast_memcpy_avx.s)
endif()

# Add GPU source if CUDA is available, otherwise use CPU fallback
if(CUDA_ENABLED)
    list(APPEND COMMON_SOURCES src/crypto/aes_256_gcm_gpu.cu)
else()
    list(APPEND COMMON_SOURCES src/crypto/aes_256_gcm_gpu_cpu.cpp)
endif()

# Create a static library for common components
add_library(net_copy_common STATIC ${COMMON_SOURCES})

# Add ASIO include directory and define standalone flag
target_include_directories(net_copy_common PUBLIC ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(net_copy_common PUBLIC ASIO_STANDALONE)

if(WIN32)
    if(WITH_RIO OR WITHRIO)
        target_compile_definitions(net_copy_common PUBLIC NETCOPY_WITH_RIO WITHRIO)
    endif()
    if(WITHIORING)
        target_compile_definitions(net_copy_common PUBLIC NETCOPY_WITH_IORING WITHIORING)
    endif()
    if(WITH_MSQUIC OR WITHMSQUIC)
        target_compile_definitions(net_copy_common PUBLIC NETCOPY_WITH_MSQUIC WITHMSQUIC)
    endif()
    if(WITH_TCP_INFO_WINDOW OR WITHTCPINFO)
        target_compile_definitions(net_copy_common PUBLIC NETCOPY_WITH_TCP_INFO_WINDOW WITHTCPINFO)
    endif()
endif()


# Try to find LZ4, otherwise continue without it (compression.cpp handles this)
# Prefer vcpkg/CONFIG package if available
find_package(lz4 CONFIG QUIET)
if(lz4_FOUND)
    target_link_libraries(net_copy_common PUBLIC lz4::lz4)
    message(STATUS "LZ4 package found (vcpkg) - compression will be enabled")
else()
    # Fallback to manual finding
    find_library(LZ4_LIBRARY NAMES lz4)
    find_path(LZ4_INCLUDE_DIR NAMES lz4.h)

    if(LZ4_LIBRARY AND LZ4_INCLUDE_DIR)
        target_link_libraries(net_copy_common PUBLIC ${LZ4_LIBRARY})
        target_include_directories(net_copy_common PUBLIC ${LZ4_INCLUDE_DIR})
        message(STATUS "LZ4 library found - compression will be enabled")
    else()
        message(WARNING "LZ4 library not found - compression will be disabled")
    endif()
endif()

# ============================================================
# wolfSSL - already installed via vcpkg (wolfssl:x64-windows@5.8.2)
# Uses the vcpkg CMake target; OpenSSL compat layer activated
# via compile definition so socket.h/socket.cpp need no changes.
# ============================================================
find_package(wolfssl CONFIG REQUIRED)
message(STATUS "wolfSSL found via vcpkg - TLS backend (replaces OpenSSL)")

# Activate the OpenSSL compatibility layer (maps SSL_CTX/SSL_*/ERR_* to wolfSSL)
# This is a preprocessor switch — the compat headers are already installed by vcpkg
target_compile_definitions(net_copy_common PUBLIC OPENSSL_EXTRA OPENSSL_ALL ASIO_USE_WOLFSSL WOLFSSL_ASIO OPENSSL_NO_SSL2 OPENSSL_NO_SSL3)

# ============================================================
# wolfSSH - SSH/SCP/SFTP server built on wolfSSL.
# Not in vcpkg, so fetched from source. Depends on wolfSSL above.
# ============================================================
set(WOLFSSH_BUILD_TESTS OFF CACHE BOOL "Skip wolfSSH tests"    FORCE)
set(WOLFSSH_EXAMPLES    OFF CACHE BOOL "Skip wolfSSH examples" FORCE)

FetchContent_Declare(
    wolfssh
    GIT_REPOSITORY https://github.com/wolfSSL/wolfssh.git
    GIT_TAG        v1.4.22-stable
    GIT_SHALLOW    ON
)
FetchContent_GetProperties(wolfssh)
if(NOT wolfssh_POPULATED)
    FetchContent_Populate(wolfssh)
endif()

# Define wolfSSH static library target manually since wolfSSH does not have a root CMakeLists.txt
set(WOLFSSH_SOURCES
    ${wolfssh_SOURCE_DIR}/src/certman.c
    ${wolfssh_SOURCE_DIR}/src/internal.c
    ${wolfssh_SOURCE_DIR}/src/io.c
    ${wolfssh_SOURCE_DIR}/src/keygen.c
    ${wolfssh_SOURCE_DIR}/src/log.c
    ${wolfssh_SOURCE_DIR}/src/port.c
    ${wolfssh_SOURCE_DIR}/src/ssh.c
    ${wolfssh_SOURCE_DIR}/src/wolfscp.c
    ${wolfssh_SOURCE_DIR}/src/wolfsftp.c
    ${wolfssh_SOURCE_DIR}/src/wolfterm.c
)

add_library(wolfssh STATIC ${WOLFSSH_SOURCES})
target_link_libraries(wolfssh PUBLIC wolfssl::wolfssl)
if(WIN32)
    target_link_libraries(wolfssh PUBLIC ws2_32)
endif()

target_include_directories(wolfssh PUBLIC
    ${wolfssh_SOURCE_DIR}
)

target_compile_definitions(wolfssh PRIVATE WOLFSSH_LIB)
target_compile_definitions(wolfssh PUBLIC WOLFSSH_SFTP WOLFSSH_SCP)
if(WIN32)
    target_compile_definitions(wolfssh PRIVATE WIN32 _LIB)
endif()

message(STATUS "wolfSSH manual target configured - optional SSH/SCP/SFTP server enabled")

# ============================================================
# Fetch liboqs for ML-KEM (post-quantum cryptography)
# OpenSSL dependency disabled - we use wolfSSL instead
# ============================================================
set(OQS_USE_OPENSSL OFF CACHE BOOL "Disable OpenSSL dependency in liboqs" FORCE)
set(OQS_BUILD_ONLY_LIB ON CACHE BOOL "Build only liboqs library" FORCE)
set(OQS_MINIMAL_BUILD "KEM_kyber;KEM_ml_kem" CACHE STRING "Build only Kyber and ML-KEM" FORCE)

FetchContent_Declare(
    liboqs
    GIT_REPOSITORY https://github.com/open-quantum-safe/liboqs.git
    GIT_TAG 0.12.0
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(liboqs)

target_link_libraries(net_copy_common PUBLIC oqs)
target_include_directories(net_copy_common PUBLIC ${liboqs_BINARY_DIR}/include)
message(STATUS "liboqs fetched - ML-KEM authentication will be enabled")

# Windows: link bcrypt for BCryptGenRandom used in sha3.cpp
if(WIN32)
    target_link_libraries(net_copy_common PUBLIC bcrypt mswsock)
endif()

# Link wolfSSL (vcpkg) and wolfSSH (FetchContent)
target_link_libraries(net_copy_common PUBLIC wolfssl::wolfssl wolfssh)
# wolfSSH headers (sources at ${wolfssh_SOURCE_DIR})
target_include_directories(net_copy_common PUBLIC
    ${wolfssh_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/include/wolfssl
)

# Set specific compilation flags for AES-CTR file to enable hardware acceleration
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set_source_files_properties(src/crypto/aes_ctr.cpp PROPERTIES
        COMPILE_FLAGS "-maes -msse2 -mavx -mavx2")
endif()

# Server executable
add_executable(net_copy_server
    src/server/main.cpp
    src/server/server.cpp
    src/daemon/daemon.cpp
)

# Client executable
add_executable(net_copy_client
    src/client/main.cpp
    src/client/client.cpp
)

# Keygen executable replaced by Admin tool
add_executable(net_copy_admin
    src/admin/main.cpp
    src/client/client.cpp
)

# Link common library to all executables
target_link_libraries(net_copy_server PRIVATE net_copy_common)
target_link_libraries(net_copy_client PRIVATE net_copy_common)
target_link_libraries(net_copy_admin PRIVATE net_copy_common)

# Windows: link ws2_32 for admin tool
if(WIN32)
    target_link_libraries(net_copy_admin PRIVATE ws2_32)
endif()

# Link CUDA libraries if available
if(CUDA_ENABLED)
    if(CUDAToolkit_FOUND)
        # Modern CMake CUDA linking
        target_link_libraries(net_copy_server PRIVATE CUDA::cudart CUDA::cuda_driver)
        target_link_libraries(net_copy_client PRIVATE CUDA::cudart CUDA::cuda_driver)
        target_link_libraries(net_copy_admin PRIVATE CUDA::cudart CUDA::cuda_driver)
    else()
        # Legacy CUDA linking
        target_link_libraries(net_copy_server PRIVATE ${CUDA_LIBRARIES})
        target_link_libraries(net_copy_client PRIVATE ${CUDA_LIBRARIES})
        target_link_libraries(net_copy_admin PRIVATE ${CUDA_LIBRARIES})
    endif()
    
    # Set CUDA properties for the common library
    set_property(TARGET net_copy_common PROPERTY CUDA_SEPARABLE_COMPILATION ON)
endif()

# Windows-specific configuration
if(WIN32)
    # Link Winsock and other Windows libraries
    target_link_libraries(net_copy_server PRIVATE ws2_32)
    target_link_libraries(net_copy_client PRIVATE ws2_32)
    
    # Windows Service executable
    add_executable(net_copy_service
        src/service/main.cpp
        src/service/windows_service.cpp
    )
    
    target_link_libraries(net_copy_service PRIVATE net_copy_common advapi32 ws2_32)
    
    # Link CUDA libraries to Windows service if available
    if(CUDA_ENABLED)
        if(CUDAToolkit_FOUND)
            target_link_libraries(net_copy_service PRIVATE CUDA::cudart CUDA::cuda_driver)
        else()
            target_link_libraries(net_copy_service PRIVATE ${CUDA_LIBRARIES})
        endif()
    endif()
    
    # Install the service executable
    install(TARGETS net_copy_service DESTINATION bin)

    # Windows GUI client
    add_executable(net_copy_gui
        src/gui/main.cpp
        src/gui/gui_server.cpp
        src/client/client.cpp
    )
    target_link_libraries(net_copy_gui PRIVATE net_copy_common ws2_32 shell32)
    
    # Link CUDA libraries to Windows GUI if available
    if(CUDA_ENABLED)
        if(CUDAToolkit_FOUND)
            target_link_libraries(net_copy_gui PRIVATE CUDA::cudart CUDA::cuda_driver)
        else()
            target_link_libraries(net_copy_gui PRIVATE ${CUDA_LIBRARIES})
        endif()
    endif()
    
    # Install the GUI executable
    install(TARGETS net_copy_gui DESTINATION bin)
endif()

# Add install targets
install(TARGETS net_copy_server net_copy_client net_copy_admin
    DESTINATION bin
)

# Add a custom target for cleaning
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Add a custom target for rebuilding
add_custom_target(rebuild_all
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/CMakeCache.txt
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CMakeFiles
    COMMAND ${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
