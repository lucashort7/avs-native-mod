@echo off
setlocal
pushd "%~dp0" || exit /b 1
rem Use an explicit MinGW setup script, the local default, or gcc already on PATH.
if defined AVS_MINGW_SETUP (
    call "%AVS_MINGW_SETUP%" >nul
    if errorlevel 1 goto :fail
) else if exist "C:\MinGW\set_distro_paths.bat" (
    call C:\MinGW\set_distro_paths.bat >nul
    if errorlevel 1 goto :fail
)
where gcc >nul 2>nul
if errorlevel 1 (
    echo ERROR: Put 64-bit Windows MinGW GCC on PATH or set AVS_MINGW_SETUP.
    goto :fail
)
where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: Put CMake 3.23 or newer on PATH.
    goto :fail
)
where ninja >nul 2>nul
if errorlevel 1 (
    echo ERROR: Put Ninja on PATH.
    goto :fail
)
cmake --preset mingw || goto :fail
cmake --build --preset mingw || goto :fail
ctest --preset mingw || goto :fail
echo built build\cmake\avs-bridge.dll and reloadable avs-native-mod.dll; all fixtures passed; no game injection
popd
exit /b 0
:fail
popd
exit /b 1
