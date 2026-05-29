@echo off
echo ========================================
echo NetCopy - Build Project Applications
echo ========================================

:: Navigate to script directory
cd /d "%~dp0"
:: Define fallback for ProgramFiles(x86)
set "PF86=%ProgramFiles(x86)%"
if not defined PF86 set "PF86=%ProgramFiles%"
:: Check for clean flag
set "CLEAN_BUILD=0"
if "%~1"=="clean" set "CLEAN_BUILD=1"
if "%~1"=="-clean" set "CLEAN_BUILD=1"
if "%~1"=="--clean" set "CLEAN_BUILD=1"
if "%~1"=="\clean" set "CLEAN_BUILD=1"
if "%~1"=="\/clean" set "CLEAN_BUILD=1"
if "%~1"=="/clean" set "CLEAN_BUILD=1"

:: Check if external dependencies exist (wolfSSL from vcpkg, wolfSSH + liboqs from FetchContent)
set "EXT_EXIST=1"
if not exist "build_vs\_deps\liboqs-build\lib\Release\oqs.lib"        set "EXT_EXIST=0"
if not exist "build_vs\vcpkg_installed\x64-windows\bin\lz4.dll"       set "EXT_EXIST=0"
if not exist "build_vs\vcpkg_installed\x64-windows\bin\wolfssl.dll"   set "EXT_EXIST=0"
if not exist "build_vs\Release\wolfssh.lib"                               set "EXT_EXIST=0"

if "%EXT_EXIST%"=="0" (
    echo [WARNING] External dependencies not found or incomplete.
    echo Running build_externals.bat first...
    call "%~dp0build_externals.bat"
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] Failed to build external dependencies!
        exit /b 1
    )
)

:: Check if already in VS Developer Command Prompt
if defined VSCMD_ARG_TGT_ARCH (
    echo [OK] Already in Visual Studio Developer Command Prompt
    echo    Architecture: %VSCMD_ARG_TGT_ARCH%
    if defined VisualStudioVersion echo    VS Version: %VisualStudioVersion%
    goto :skip_vs_setup
)

:: Check for Visual Studio installations
set "VS_FOUND="
set "VS_PATH="
set "VS_VERSION="

echo Detecting Visual Studio installations...

:: 1. Try vswhere (most robust)
if exist "%PF86%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%PF86%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VS_PATH=%%i"
            set "VS_VERSION=Auto-detected via vswhere"
            set "VS_FOUND=1"
            goto :vs_found
        )
    )
)

:: 2. Check common paths (VS 2025/2026/Latest, VS 2022, VS 2019)
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Community"
    set "VS_VERSION=18 Community"
    set "VS_FOUND=1"
    goto :vs_found
)
if exist "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Professional"
    set "VS_VERSION=18 Professional"
    set "VS_FOUND=1"
    goto :vs_found
)
if exist "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Enterprise"
    set "VS_VERSION=18 Enterprise"
    set "VS_FOUND=1"
    goto :vs_found
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
    set "VS_VERSION=18 Build Tools"
    set "VS_FOUND=1"
    goto :vs_found
)
if exist "C:\Program Files\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\18\BuildTools"
    set "VS_VERSION=18 Build Tools"
    set "VS_FOUND=1"
    goto :vs_found
)
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
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
    set "VS_VERSION=2019 Community"
    set "VS_FOUND=1"
    goto :vs_found
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    set "VS_VERSION=2022 Build Tools"
    set "VS_FOUND=1"
    goto :vs_found
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
    set "VS_VERSION=2022 Build Tools"
    set "VS_FOUND=1"
    goto :vs_found
)

:vs_not_found
echo [ERROR] Visual Studio not found!
echo Please install Visual Studio 2022/2025 or Build Tools, then try again.
exit /b 1

:vs_found
echo [OK] Found: %VS_VERSION%
echo Path: %VS_PATH%
echo.

:: Setup Visual Studio environment
echo Setting up Visual Studio environment...
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to setup Visual Studio environment
    exit /b 1
)

:skip_vs_setup
echo [OK] Visual Studio environment ready
echo.

:: Create build directory if needed
if not exist build_vs mkdir build_vs

:: Preserve runtime dependencies (configs, certs, keys, DLLs)
echo Preserving runtime dependencies...
set "PRESERVE_DIR=%TEMP%\netcopy_preserve_%RANDOM%"
mkdir "%PRESERVE_DIR%" 2>nul
if exist "build_vs\*.conf" xcopy /Y "build_vs\*.conf" "%PRESERVE_DIR%\" >nul 2>&1
if exist "build_vs\*.pem"  xcopy /Y "build_vs\*.pem"  "%PRESERVE_DIR%\" >nul 2>&1
if exist "build_vs\*.csv"  xcopy /Y "build_vs\*.csv"  "%PRESERVE_DIR%\" >nul 2>&1
if exist "build_vs\*.dll"  xcopy /Y "build_vs\*.dll"  "%PRESERVE_DIR%\" >nul 2>&1

:: Clean only project build artifacts if clean requested
if "%CLEAN_BUILD%"=="1" (
    echo Cleaning project build artifacts...
    if exist "build_vs\Release" (
        echo   Removing build_vs\Release...
        rmdir /s /q "build_vs\Release" >nul 2>&1
    )
    if exist "build_vs\*.exe" (
        echo   Removing build_vs\*.exe...
        del /q "build_vs\*.exe" >nul 2>&1
    )
)

:: Restore preserved files
echo Restoring runtime dependencies...
if defined PRESERVE_DIR if exist "%PRESERVE_DIR%\*" xcopy /Y "%PRESERVE_DIR%\*" "build_vs\" >nul 2>&1
if defined PRESERVE_DIR rmdir /s /q "%PRESERVE_DIR%" >nul 2>&1

:: Determine generator and toolchain (needed if CMakeCache.txt does not exist)
if defined VS_PATH set "VS_PATH=%VS_PATH:"=%"
if exist build_vs\CMakeCache.txt goto :skip_cmake_config

echo [WARNING] CMake Cache not found. Configuring CMake first...

if "%VCPKG_ROOT%"=="" if exist "%VS_PATH%\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_ROOT=%VS_PATH%\VC\vcpkg"
if "%VCPKG_ROOT%"=="" if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_ROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg"
if "%VCPKG_ROOT%"=="" if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_ROOT=C:\vcpkg"

set "TOOLCHAIN_OPT="
if "%VCPKG_ROOT%"=="" goto :skip_vcpkg_setup
set "TOOLCHAIN_OPT=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
echo   Using vcpkg toolchain: "%VCPKG_ROOT%"
:skip_vcpkg_setup

set "VS_GENERATOR="
if "%VS_VERSION%"=="" goto :detect_generator_from_path
echo "%VS_VERSION%" | findstr /i "18" >nul && set "VS_GENERATOR=Visual Studio 18 2026"
echo "%VS_VERSION%" | findstr /i "2022" >nul && set "VS_GENERATOR=Visual Studio 17 2022"
echo "%VS_VERSION%" | findstr /i "2019" >nul && set "VS_GENERATOR=Visual Studio 16 2019"

:detect_generator_from_path
if not "%VS_GENERATOR%"=="" goto :generator_done
if "%VS_PATH%"=="" goto :generator_done
echo "%VS_PATH%" | findstr /i "\\18\\" >nul && set "VS_GENERATOR=Visual Studio 18 2026"
echo "%VS_PATH%" | findstr /i "\\2022\\" >nul && set "VS_GENERATOR=Visual Studio 17 2022"
echo "%VS_PATH%" | findstr /i "\\2019\\" >nul && set "VS_GENERATOR=Visual Studio 16 2019"

:generator_done
if "%VS_GENERATOR%"=="" set "VS_GENERATOR=Visual Studio 17 2022"

echo %VS_GENERATOR%>build_vs\.last_generator

set "FC_BASE_DIR=%~dp0build_vs\_deps"
cd build_vs
if not "%TOOLCHAIN_OPT%"=="" (
    cmake -G "%VS_GENERATOR%" -A x64 "-DFETCHCONTENT_BASE_DIR=%FC_BASE_DIR%" "%TOOLCHAIN_OPT%" ..
) else (
    cmake -G "%VS_GENERATOR%" -A x64 "-DFETCHCONTENT_BASE_DIR=%FC_BASE_DIR%" ..
)
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed!
    cd ..
    exit /b 1
)
cd ..

:skip_cmake_config

cd build_vs

:: Build project
echo Building project with Visual Studio compiler...
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed!
    cd ..
    exit /b 1
)

:: Copy executables and DLLs to build root
if exist Release\*.exe copy /y Release\*.exe . >nul
if exist vcpkg_installed\x64-windows\bin\*.dll copy /y vcpkg_installed\x64-windows\bin\*.dll . >nul

echo.
echo ========================================
echo Build completed successfully!
echo ========================================
echo Executables created:
dir /b *.exe 2>nul
echo.

echo ========================================
echo Build Information:
echo ========================================
if not defined VisualStudioVersion goto :print_sdk_version
echo Compiler: Microsoft Visual C++ (VS %VisualStudioVersion%)
goto :print_compiler_done
:print_sdk_version
if defined VS_VERSION (
    echo Compiler: Microsoft Visual C++ (%VS_VERSION%)
) else (
    echo Compiler: Microsoft Visual C++
)
:print_compiler_done
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

echo To test:
echo   net_copy_server.exe -p 1245
echo   net_copy_client.exe -s aes -v file.txt 127.0.0.1:1245/
echo.

cd ..
exit /b 0
