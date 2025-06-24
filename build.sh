#!/bin/bash

# NetCopy Build Script
set -e

echo "Building NetCopy..."

# Create build directory
mkdir -p build
cd build

# Compiler settings
CXX="g++"
CXXFLAGS="-std=c++17 -Wall -Wextra -O2 -I../include -maes -msse2 -mavx"
LDFLAGS=""

# Check if compiler supports AVX2 and add it
if $CXX -mavx2 -x c++ -E - < /dev/null >/dev/null 2>&1; then
    CXXFLAGS="$CXXFLAGS -mavx2"
    echo "AVX2 support detected and enabled"
fi

# Define source files
COMMON_SOURCES=(
    "../src/crypto/chacha20_poly1305.cpp"
    "../src/crypto/xor_cipher.cpp" 
    "../src/crypto/aes_ctr.cpp"
    "../src/crypto/crypto_engine.cpp"
    "../src/network/socket.cpp"
    "../src/protocol/message.cpp"
    "../src/file/file_manager.cpp"
    "../src/config/config_parser.cpp"
    "../src/logging/logger.cpp"
    "../src/common/utils.cpp"
    "../src/common/bandwidth_monitor.cpp"
)

# Check platform and add platform-specific sources
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
    echo "Building for Windows..."
    CXXFLAGS="$CXXFLAGS -D_WIN32"
    LDFLAGS="$LDFLAGS -lws2_32 -lmswsock"
else
    echo "Building for Unix/Linux..."
    # Use daemon stub for now due to compilation issues
    COMMON_SOURCES+=("../daemon_stub.cpp")
fi

echo "Compiling common library..."
# Compile common sources into object files
for src in "${COMMON_SOURCES[@]}"; do
    obj=$(basename "$src" .cpp).o
    echo "  Compiling $src -> $obj"
    $CXX $CXXFLAGS -c "$src" -o "$obj"
done

# Create static library
echo "Creating static library..."
ar rcs libnet_copy_common.a *.o

echo "Building client executable..."
$CXX $CXXFLAGS "../src/client/main.cpp" "../src/client/client.cpp" -L. -lnet_copy_common $LDFLAGS -o net_copy_client

echo "Building server executable..."
$CXX $CXXFLAGS "../src/server/main.cpp" "../src/server/server.cpp" -L. -lnet_copy_common $LDFLAGS -o net_copy_server

echo "Building keygen executable..."
$CXX $CXXFLAGS "../src/keygen/main.cpp" -L. -lnet_copy_common $LDFLAGS -o net_copy_keygen

# Check if Windows service is needed
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
    echo "Building Windows service executable..."
    $CXX $CXXFLAGS "../src/service/main.cpp" "../src/service/windows_service.cpp" -L. -lnet_copy_common $LDFLAGS -o net_copy_service
fi

echo "Build completed successfully!"
echo "Executables created in build/ directory:"
ls -la net_copy_*

cd ..