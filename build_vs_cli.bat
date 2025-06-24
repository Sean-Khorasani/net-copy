@echo off
echo ========================================
echo Building NetCopy with Visual Studio CLI
echo ========================================

:: Check for Visual Studio installations
set "VS_FOUND="
set "VS_PATH="
set "VS_VERSION="

echo Detecting Visual Studio installations...

:: Check for VS 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
    set "VS_VERSION=2022 Community"
    set "VS_FOUND=1"
    goto :vs_found
)

if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional"
    set "VS_VERSION=2022 Professional"
    set "VS_FOUND=1"
    goto :vs_found
)

if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
    set "VS_VERSION=2022 Enterprise"
    set "VS_FOUND=1"
    goto :vs_found
)

:: Check for VS 2019
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
    set "VS_VERSION=2019 Community"
    set "VS_FOUND=1"
    goto :vs_found
)

if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional"
    set "VS_VERSION=2019 Professional"
    set "VS_FOUND=1"
    goto :vs_found
)

:: Check for Build Tools only
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    set "VS_VERSION=2022 Build Tools"
    set "VS_FOUND=1"
    goto :vs_found
)

if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools"
    set "VS_VERSION=2019 Build Tools"
    set "VS_FOUND=1"
    goto :vs_found
)

:vs_not_found
echo ❌ Visual Studio not found!
echo.
echo Please install one of:
echo 1. Visual Studio Community 2022 (free): https://visualstudio.microsoft.com/vs/community/
echo 2. Build Tools for Visual Studio 2022 (free): https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
echo.
echo After installation, run this script again.
pause
exit /b 1

:vs_found
echo ✅ Found: %VS_VERSION%
echo Path: %VS_PATH%
echo.

:: Setup Visual Studio environment
echo Setting up Visual Studio environment...
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to setup Visual Studio environment
    pause
    exit /b 1
)

echo ✅ Visual Studio environment ready
echo.

:: Navigate to project directory
cd /d "%~dp0"

:: Clean previous build
if exist build_vs (
    echo Cleaning previous Visual Studio build...
    rmdir /s /q build_vs >nul 2>&1
)
mkdir build_vs
cd build_vs

:: Configure with CMake using Ninja generator (faster than MSBuild)
echo Configuring build with Visual Studio compiler (Ninja)...
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release ..
if %ERRORLEVEL% neq 0 (
    echo.
    echo Ninja not found, trying with NMake...
    cd ..
    rmdir /s /q build_vs >nul 2>&1
    mkdir build_vs
    cd build_vs
    
    cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ..
    if %ERRORLEVEL% neq 0 (
        echo ERROR: CMake configuration failed!
        pause
        exit /b 1
    )
)

:: Build
echo Building project with Visual Studio compiler...
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build completed successfully!
echo ========================================
echo Executables created:
dir /b *.exe
echo.

echo ========================================
echo Build Information:
echo ========================================
echo Compiler: Microsoft Visual C++ (%VS_VERSION%)
echo Generator: Ninja or NMake Makefiles
echo Architecture: x64
echo Configuration: Release
echo.

echo ========================================
echo Available Security Modes:
echo ========================================
echo   -s high        ChaCha20-Poly1305 (most secure)
echo   -s fast        XOR cipher (fastest, less secure)  
echo   -s aes         AES-CTR with AES-NI (balanced)
echo   -s AES-256-GCM AES-256-GCM with CPU fallback
echo.

echo ========================================
echo Benefits of Visual Studio Build:
echo ========================================
echo ✅ Better Windows compatibility
echo ✅ Native Windows networking support
echo ✅ Optimized for Windows performance
echo ✅ No MinGW compatibility issues
echo ✅ Ready for CUDA (if desired later)
echo.

echo To test:
echo   net_copy_server.exe -p 1245
echo   net_copy_client.exe -s aes -v file.txt 127.0.0.1:1245/
echo.
pause