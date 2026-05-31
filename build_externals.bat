@echo off
echo ========================================
echo NetCopy - Build External Dependencies
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

:: Parse arguments
set "FORCE_BUILD=0"
if "%~1"=="--force" set "FORCE_BUILD=1"
if "%~1"=="-f" set "FORCE_BUILD=1"
if "%~1"=="force" set "FORCE_BUILD=1"
if "%CLEAN_BUILD%"=="1" set "FORCE_BUILD=1"

if "%CLEAN_BUILD%"=="1" (
    echo Clean external build requested. Removing generated dependency state...
    if exist "build_vs\CMakeCache.txt" del /f /q "build_vs\CMakeCache.txt"
    if exist "build_vs\CMakeFiles" rmdir /s /q "build_vs\CMakeFiles"
    if exist "build_vs\_deps" rmdir /s /q "build_vs\_deps"
    if exist "build_vs\vcpkg_installed" rmdir /s /q "build_vs\vcpkg_installed"
    if exist "build_vs\Release\wolfssh.lib" del /f /q "build_vs\Release\wolfssh.lib"
    if exist "build_vs\Release\oqs.lib" del /f /q "build_vs\Release\oqs.lib"
)

:: Quick check if dependencies already exist
:: wolfSSL comes from vcpkg; wolfSSH and liboqs are FetchContent
set "EXT_EXIST=1"
if not exist "build_vs\_deps\liboqs-build\lib\Release\oqs.lib"        set "EXT_EXIST=0"
if not exist "build_vs\vcpkg_installed\x64-windows\bin\lz4.dll"       set "EXT_EXIST=0"
if not exist "build_vs\vcpkg_installed\x64-windows\bin\wolfssl.dll"   set "EXT_EXIST=0"
if not exist "build_vs\Release\wolfssh.lib"                               set "EXT_EXIST=0"

if "%EXT_EXIST%"=="0" goto :do_build
if "%FORCE_BUILD%"=="1" (
    echo Force build requested. Rebuilding external dependencies...
    goto :do_build
)

echo [OK] External libraries already built:
echo   - lz4       (compression, via vcpkg)
echo   - wolfSSL   (TLS 1.3 - replaces OpenSSL, via vcpkg)
echo   - wolfSSH   (optional SSH/SCP/SFTP server, FetchContent)
echo   - liboqs    (ML-KEM post-quantum crypto, FetchContent)
echo.
echo If you want to force rebuild them, run: %~nx0 --force
echo If the dependency cache is damaged, run: %~nx0 --clean
exit /b 0

:do_build

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
:: Try to find vcpkg toolchain
if "%VCPKG_ROOT%"=="" if exist "%VS_PATH%\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_ROOT=%VS_PATH%\VC\vcpkg"
if "%VCPKG_ROOT%"=="" if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_ROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg"
if "%VCPKG_ROOT%"=="" if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_ROOT=C:\vcpkg"

set "TOOLCHAIN_OPT="
if "%VCPKG_ROOT%"=="" goto :skip_vcpkg_toolchain
set "TOOLCHAIN_OPT=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
echo   Using vcpkg toolchain: %VCPKG_ROOT%
:skip_vcpkg_toolchain

echo [OK] Visual Studio environment ready
echo.

:: Create build directory if needed
if not exist build_vs mkdir build_vs

:: Use isolated FetchContent cache for Visual Studio build
set "FC_BASE_DIR=%~dp0build_vs\_deps"
echo Using FetchContent cache: %FC_BASE_DIR%

:: Determine correct generator name
set "VS_GENERATOR="
if "%VS_VERSION%"=="" goto :detect_generator_from_path
echo %VS_VERSION% | findstr /i "18" >nul && set "VS_GENERATOR=Visual Studio 18 2026"
echo %VS_VERSION% | findstr /i "2022" >nul && set "VS_GENERATOR=Visual Studio 17 2022"
echo %VS_VERSION% | findstr /i "2019" >nul && set "VS_GENERATOR=Visual Studio 16 2019"

:detect_generator_from_path
if not "%VS_GENERATOR%"=="" goto :generator_detected
if "%VS_PATH%"=="" goto :generator_detected
echo %VS_PATH% | findstr /i "\\18\\" >nul && set "VS_GENERATOR=Visual Studio 18 2026"
echo %VS_PATH% | findstr /i "\\2022\\" >nul && set "VS_GENERATOR=Visual Studio 17 2022"
echo %VS_PATH% | findstr /i "\\2019\\" >nul && set "VS_GENERATOR=Visual Studio 16 2019"

:generator_detected
if "%VS_GENERATOR%"=="" set "VS_GENERATOR=Visual Studio 17 2022"

set "LAST_GENERATOR="
if exist build_vs\.last_generator set /p LAST_GENERATOR=<build_vs\.last_generator

if "%LAST_GENERATOR%"=="" goto :skip_generator_clean
if "%LAST_GENERATOR%"=="%VS_GENERATOR%" goto :skip_generator_clean

echo [WARNING] CMake generator mismatch detected:
echo   Last generator: %LAST_GENERATOR%
echo   New generator:  %VS_GENERATOR%
echo Cleaning build_vs to allow CMake reinitialize...

:: First kill any git processes that may have files locked
taskkill /f /im git.exe >nul 2>&1

:: Remove CMakeCache.txt (this is what CMake validates the generator against)
if exist "build_vs\CMakeCache.txt" del /f /q "build_vs\CMakeCache.txt"

:: Remove CMakeFiles directory
if exist "build_vs\CMakeFiles" rmdir /s /q "build_vs\CMakeFiles"

:: Do not remove FetchContent cache to avoid deleting needful dependencies
:: We only removed CMakeCache.txt and CMakeFiles, which is sufficient for generator change

:skip_generator_clean
echo %VS_GENERATOR%>build_vs\.last_generator

echo Configuring build with Visual Studio generator: %VS_GENERATOR%
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

:: Build external targets:
::   wolfSSL  - already installed via vcpkg (cmake configure handles it)
::   wolfssh  - SSH/SFTP server (FetchContent, built on wolfSSL)
::   oqs      - post-quantum crypto (FetchContent)
echo Building wolfSSH (optional SSH/SCP/SFTP server)...
cmake --build . --target wolfssh --config Release -j %NUMBER_OF_PROCESSORS%
if %ERRORLEVEL% neq 0 (
    echo ERROR: wolfSSH build failed!
    cd ..
    exit /b 1
)

echo Building liboqs (post-quantum ML-KEM)...
cmake --build . --target oqs --config Release -j %NUMBER_OF_PROCESSORS%
if %ERRORLEVEL% neq 0 (
    echo ERROR: liboqs build failed!
    cd ..
    exit /b 1
)

:: Copy vcpkg DLLs (lz4, wolfssl) to the build root
if exist vcpkg_installed\x64-windows\bin\*.dll (
    echo Copying vcpkg DLLs lz4 + wolfssl to build root...
    copy /y vcpkg_installed\x64-windows\bin\*.dll . >nul
)

echo.
echo ========================================
echo External libraries built successfully!
echo   wolfSSL:  via vcpkg (wolfssl.dll, replaces 3 OpenSSL DLLs)
echo   wolfSSH:  FetchContent (optional SSH/SCP/SFTP)
echo   liboqs:   FetchContent (ML-KEM post-quantum crypto)
echo ========================================
cd ..
exit /b 0
