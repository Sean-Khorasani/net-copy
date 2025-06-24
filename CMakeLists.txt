cmake_minimum_required(VERSION 3.10)

project(NetCopy CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CUDA configuration (optional)
option(ENABLE_CUDA "Enable CUDA support for GPU acceleration" OFF)

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
endif()

# Common include directories
include_directories(include)

# Common source files
set(COMMON_SOURCES
    src/crypto/chacha20_poly1305.cpp
    src/crypto/xor_cipher.cpp
    src/crypto/aes_ctr.cpp
    src/crypto/crypto_engine.cpp
    src/network/socket.cpp
    src/protocol/message.cpp
    src/file/file_manager.cpp
    src/config/config_parser.cpp
    src/logging/logger.cpp
    src/common/utils.cpp
    src/common/bandwidth_monitor.cpp
)

# Add GPU source if CUDA is available, otherwise use CPU fallback
if(CUDA_ENABLED)
    list(APPEND COMMON_SOURCES src/crypto/aes_256_gcm_gpu.cu)
else()
    list(APPEND COMMON_SOURCES src/crypto/aes_256_gcm_gpu_cpu.cpp)
endif()

# Create a static library for common components
add_library(net_copy_common STATIC ${COMMON_SOURCES})

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

# Keygen executable
add_executable(net_copy_keygen
    src/keygen/main.cpp
)

# Link common library to all executables
target_link_libraries(net_copy_server PRIVATE net_copy_common)
target_link_libraries(net_copy_client PRIVATE net_copy_common)
target_link_libraries(net_copy_keygen PRIVATE net_copy_common)

# Link CUDA libraries if available
if(CUDA_ENABLED)
    if(CUDAToolkit_FOUND)
        # Modern CMake CUDA linking
        target_link_libraries(net_copy_server PRIVATE CUDA::cudart CUDA::cuda_driver)
        target_link_libraries(net_copy_client PRIVATE CUDA::cudart CUDA::cuda_driver)
        target_link_libraries(net_copy_keygen PRIVATE CUDA::cudart CUDA::cuda_driver)
    else()
        # Legacy CUDA linking
        target_link_libraries(net_copy_server PRIVATE ${CUDA_LIBRARIES})
        target_link_libraries(net_copy_client PRIVATE ${CUDA_LIBRARIES})
        target_link_libraries(net_copy_keygen PRIVATE ${CUDA_LIBRARIES})
    endif()
    
    # Set CUDA properties for the common library
    set_property(TARGET net_copy_common PROPERTY CUDA_SEPARABLE_COMPILATION ON)
endif()

# Windows-specific configuration
if(WIN32)
    # Link Winsock and other Windows libraries
    target_link_libraries(net_copy_server PRIVATE ws2_32)
    target_link_libraries(net_copy_client PRIVATE ws2_32)
    target_link_libraries(net_copy_keygen PRIVATE ws2_32)
    
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
endif()

# Add install targets
install(TARGETS net_copy_server net_copy_client net_copy_keygen
    DESTINATION bin
)

# Add a custom target for cleaning
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Add a custom target for rebuilding
add_custom_target(rebuild_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)


