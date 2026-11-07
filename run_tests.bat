@echo off
setlocal enabledelayedexpansion
chcp 65001 > nul

pushd "%~dp0"
set BUILD_DIR=Output\build-windows-debug

if not defined VSCMD_VER (
    if exist "D:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
        call "D:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
    ) else if exist "D:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
        call "D:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
    ) else if exist "E:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
        call "E:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
    )
)

echo.
echo ======================================================================
echo   Executing MQEngine Verification Pipeline (All Suites M1 - M5)
echo ======================================================================

cmake --build "%BUILD_DIR%" --target run_all_tests
set TEST_EXIT_CODE=!ERRORLEVEL!

if !TEST_EXIT_CODE! equ 0 (
    echo.
    echo ======================================================================
    echo   ALL 13 VERIFICATION TEST SUITES PASSED [100%% GREEN]
    echo ======================================================================
) else (
    echo.
    echo ======================================================================
    echo   TEST SUITE EXECUTION FAILED WITH EXIT CODE: !TEST_EXIT_CODE!
    echo ======================================================================
)

popd
exit /b !TEST_EXIT_CODE!
