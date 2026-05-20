@echo off
echo ========================================
echo Auto-detecting CUDA compatible compiler
echo ========================================

:: Save original PATH
set ORIGINAL_PATH=%PATH%

:: Check for TDM-GCC
if exist "C:\TDM-GCC-64\bin\gcc.exe" (
    echo Found TDM-GCC 64-bit
    set "PATH=C:\TDM-GCC-64\bin;%ORIGINAL_PATH%"
    set COMPILER_TYPE=TDM-GCC-64
    goto build_cuda
)

if exist "C:\TDM-GCC\bin\gcc.exe" (
    echo Found TDM-GCC
    set "PATH=C:\TDM-GCC\bin;%ORIGINAL_PATH%"
    set COMPILER_TYPE=TDM-GCC
    goto build_cuda
)

:: Check for traditional MinGW
if exist "C:\MinGW\bin\gcc.exe" (
    echo Found traditional MinGW
    set "PATH=C:\MinGW\bin;%ORIGINAL_PATH%"
    set COMPILER_TYPE=MinGW
    goto build_cuda
)

:: Check for Qt MinGW
if exist "C:\Qt\Tools\mingw810_64\bin\gcc.exe" (
    echo Found Qt MinGW
    set "PATH=C:\Qt\Tools\mingw810_64\bin;%ORIGINAL_PATH%"
    set COMPILER_TYPE=Qt-MinGW
    goto build_cuda
)

:: Check for MSYS2 traditional MinGW (not UCRT64)
if exist "C:\msys64\mingw64\bin\gcc.exe" (
    echo Found MSYS2 MinGW64 (trying anyway)
    set "PATH=C:\msys64\mingw64\bin;%ORIGINAL_PATH%"
    set COMPILER_TYPE=MSYS2-MinGW64
    goto try_cuda
)

:: No compatible compiler found
echo ========================================
echo No CUDA-compatible compiler found
echo ========================================
echo.
echo Your current UCRT64 MinGW is not compatible with CUDA.
echo.
echo To enable GPU acceleration, install one of:
echo - TDM-GCC: https://jmeubank.github.io/tdm-gcc/
echo - Traditional MinGW: https://osdn.net/projects/mingw/
echo - Visual Studio Build Tools (free)
echo.
echo Building without CUDA (CPU fallback will be used)...
echo.
pause

call build_no_cuda.bat
goto end

:try_cuda
echo WARNING: %COMPILER_TYPE% may not be fully compatible with CUDA
echo Attempting build anyway...
goto build_cuda

:build_cuda
echo Using %COMPILER_TYPE% for CUDA build
echo.

:: Set CUDA environment
set CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v11.8
set PATH=%CUDA_PATH%\bin;%PATH%

:: Test compiler
gcc --version
echo.

:: Test NVCC with this compiler
echo Testing NVCC compatibility...
for /f "tokens=*" %%i in ('where gcc') do set GCC_PATH=%%i
nvcc -ccbin="%GCC_PATH%" --version
if %ERRORLEVEL% neq 0 (
    echo ERROR: NVCC is not compatible with %COMPILER_TYPE%
    echo Falling back to CPU-only build...
    call build_no_cuda.bat
    goto end
)

:: Navigate to project directory
cd /d "%~dp0"

:: Clean previous build
if exist build (
    echo Cleaning previous build...
    timeout /t 2 /nobreak >nul 2>&1
    rmdir /s /q build >nul 2>&1
)
mkdir build
cd build

:: Configure with CMake
echo Configuring CUDA build with %COMPILER_TYPE%...
cmake -G "MinGW Makefiles" ^
      -DENABLE_CUDA=ON ^
      -DCMAKE_CUDA_HOST_COMPILER="%GCC_PATH%" ^
      ..
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed with %COMPILER_TYPE%
    echo Falling back to CPU-only build...
    cd ..
    call build_no_cuda.bat
    goto end
)

:: Build
echo Building with CUDA support...
cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed with %COMPILER_TYPE%
    echo Falling back to CPU-only build...
    cd ..
    call build_no_cuda.bat
    goto end
)

echo ========================================
echo CUDA build completed successfully!
echo Using: %COMPILER_TYPE%
echo ========================================
echo Executables created:
dir /b *.exe
echo.
echo GPU acceleration should be available with:
echo net_copy_client.exe -s AES-256-GCM -v file.txt server:path
echo.

:end
:: Restore original PATH
set PATH=%ORIGINAL_PATH%
pause