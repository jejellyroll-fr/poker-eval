@echo off
REM Build script for poker-eval using CMake on Windows

setlocal enabledelayedexpansion

REM Default values
set BUILD_TYPE=Release
set BUILD_DIR=build
set PRESET=
set CLEAN=0
set VERBOSE=0
set PARALLEL=%NUMBER_OF_PROCESSORS%
set CMAKE_ARGS=

REM Parse command line arguments
:parse_args
if "%~1"=="" goto :end_parse
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="--help" goto :usage
if /i "%~1"=="-t" (
    set BUILD_TYPE=%~2
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="--type" (
    set BUILD_TYPE=%~2
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="-d" (
    set BUILD_DIR=%~2
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="--dir" (
    set BUILD_DIR=%~2
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="-p" (
    set PRESET=%~2
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="--preset" (
    set PRESET=%~2
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="-c" (
    set CLEAN=1
    shift
    goto :parse_args
)
if /i "%~1"=="--clean" (
    set CLEAN=1
    shift
    goto :parse_args
)
if /i "%~1"=="-v" (
    set VERBOSE=1
    set CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_VERBOSE_MAKEFILE=ON
    shift
    goto :parse_args
)
if /i "%~1"=="--verbose" (
    set VERBOSE=1
    set CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_VERBOSE_MAKEFILE=ON
    shift
    goto :parse_args
)
if /i "%~1"=="-j" (
    set PARALLEL=%~2
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="--jobs" (
    set PARALLEL=%~2
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="--shared" (
    set CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF
    shift
    goto :parse_args
)
if /i "%~1"=="--static" (
    set CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON
    shift
    goto :parse_args
)
if /i "%~1"=="--no-tests" (
    set CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_TESTS=OFF
    shift
    goto :parse_args
)
if /i "%~1"=="--no-examples" (
    set CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_EXAMPLES=OFF
    shift
    goto :parse_args
)
if /i "%~1"=="--gpu" (
    set CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_GPU=ON
    shift
    goto :parse_args
)
if /i "%~1"=="--lto" (
    set CMAKE_ARGS=%CMAKE_ARGS% -DENABLE_LTO=ON
    shift
    goto :parse_args
)
if /i "%~1"=="--five-cards" (
    set CMAKE_ARGS=%CMAKE_ARGS% -DUSE_FIVE_CARDS=ON
    shift
    goto :parse_args
)
echo Unknown option: %~1
goto :usage

:end_parse

REM Check for CMake
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] CMake is not installed or not in PATH.
    echo Please install CMake 3.16 or later from https://cmake.org/
    exit /b 1
)

REM Clean build directory if requested
if %CLEAN%==1 (
    echo [INFO] Cleaning build directory: %BUILD_DIR%
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

REM Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM Configure
echo [INFO] Configuring poker-eval...
echo [INFO] Build type: %BUILD_TYPE%
echo [INFO] Build directory: %BUILD_DIR%

if not "%PRESET%"=="" (
    echo [INFO] Using preset: %PRESET%
    cmake --preset "%PRESET%" %CMAKE_ARGS%
    if %errorlevel% neq 0 exit /b %errorlevel%
) else (
    cd "%BUILD_DIR%"
    cmake .. -DCMAKE_BUILD_TYPE=%BUILD_TYPE% %CMAKE_ARGS%
    if %errorlevel% neq 0 (
        cd ..
        exit /b %errorlevel%
    )
    cd ..
)

REM Build
echo [INFO] Building with %PARALLEL% parallel jobs...
if not "%PRESET%"=="" (
    cmake --build --preset "%PRESET%" -j %PARALLEL%
) else (
    cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% -j %PARALLEL%
)

if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    exit /b %errorlevel%
)

echo [INFO] Build completed successfully!
echo.
echo Next steps:
echo   - Run tests: cd %BUILD_DIR% ^&^& ctest -C %BUILD_TYPE%
echo   - Install: cd %BUILD_DIR% ^&^& cmake --install . --config %BUILD_TYPE%
echo   - Run examples: cd %BUILD_DIR%\examples\%BUILD_TYPE% ^&^& pokenum.exe

goto :eof

:usage
echo Usage: %~nx0 [OPTIONS]
echo.
echo Build poker-eval library using CMake
echo.
echo OPTIONS:
echo     -h, --help          Show this help message
echo     -t, --type TYPE     Build type (Debug, Release, RelWithDebInfo, MinSizeRel)
echo                         Default: Release
echo     -d, --dir DIR       Build directory (default: build)
echo     -p, --preset PRESET Use CMake preset (e.g., windows-msvc)
echo     -c, --clean         Clean build directory before building
echo     -v, --verbose       Enable verbose output
echo     -j, --jobs N        Number of parallel jobs (default: auto-detect)
echo     --shared            Build shared libraries only
echo     --static            Build static libraries only
echo     --no-tests          Disable building tests
echo     --no-examples       Disable building examples
echo     --gpu               Enable GPU acceleration support
echo     --lto               Enable Link Time Optimization
echo     --five-cards        Use five cards mode (default is 7)
echo.
echo EXAMPLES:
echo     REM Basic release build
echo     %~nx0
echo.
echo     REM Debug build
echo     %~nx0 --type Debug
echo.
echo     REM Build using Visual Studio preset
echo     %~nx0 --preset windows-msvc
echo.
echo     REM Clean build with GPU support
echo     %~nx0 --clean --gpu
exit /b 0