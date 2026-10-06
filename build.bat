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
if not exist build mkdir build
if errorlevel 1 goto :fail
set "FLAGS=-O2 -Wall -Wextra -Werror -Isrc -Ivendor\minhook\include"
set "COMMON=src\probe.c src\logger.c vendor\minhook\buffer.c vendor\minhook\hook.c vendor\minhook\trampoline.c vendor\minhook\hde\hde64.c"
gcc %FLAGS% tests\hook_fixture.c %COMMON% -o build\hook_fixture.exe || goto :fail
gcc %FLAGS% -shared src\dll.c %COMMON% -o build\avs-native-mod.dll || goto :fail
gcc %FLAGS% tests\loader_fixture.c -o build\loader_fixture.exe || goto :fail
pushd build || goto :fail
hook_fixture.exe guard || goto :fail_build
hook_fixture.exe disabled || goto :fail_build
hook_fixture.exe || goto :fail_build
loader_fixture.exe || goto :fail_build
popd
echo built build\avs-native-mod.dll; all fixtures passed; no game injection
popd
exit /b 0
:fail_build
popd
:fail
popd
exit /b 1
