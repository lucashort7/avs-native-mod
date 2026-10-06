cmake_minimum_required(VERSION 3.23)
project(avs_native_mod LANGUAGES C)

if(NOT WIN32 OR NOT MINGW OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Use native Windows CMake with 64-bit MinGW GCC. See README.md.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(minhook STATIC
    vendor/minhook/buffer.c
    vendor/minhook/hook.c
    vendor/minhook/trampoline.c
    vendor/minhook/hde/hde64.c
)
target_include_directories(minhook SYSTEM PUBLIC vendor/minhook/include)
target_compile_options(minhook PRIVATE -O2)

# Share object files between the DLL and its native integration fixture.
add_library(probe_core OBJECT src/probe.c src/logger.c src/inventory.c)
target_include_directories(probe_core PUBLIC src)
target_link_libraries(probe_core PUBLIC minhook)

add_library(avs-native-mod SHARED src/dll.c)
target_link_libraries(avs-native-mod PRIVATE probe_core)
set_target_properties(avs-native-mod PROPERTIES PREFIX "")

add_executable(hook_fixture tests/hook_fixture.c)
target_link_libraries(hook_fixture PRIVATE probe_core)
add_executable(loader_fixture tests/loader_fixture.c)
add_dependencies(loader_fixture avs-native-mod)
add_executable(logger_fixture tests/logger_fixture.c src/logger.c)
target_include_directories(logger_fixture PRIVATE src)
target_compile_definitions(logger_fixture PRIVATE AVS_LOGGER_TESTING)
add_executable(inventory_fixture tests/inventory_fixture.c src/inventory.c)
target_include_directories(inventory_fixture PRIVATE src)
target_compile_definitions(inventory_fixture PRIVATE INVENTORY_CAPACITY=8u)

foreach(target probe_core avs-native-mod hook_fixture loader_fixture logger_fixture inventory_fixture)
    target_compile_options(${target} PRIVATE -O2 -Wall -Wextra -Werror)
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME hook.guard COMMAND hook_fixture guard)
    add_test(NAME hook.disabled COMMAND hook_fixture disabled)
    add_test(NAME hook.concurrent COMMAND hook_fixture)
    add_test(NAME dll.wrong-host COMMAND loader_fixture)
    add_test(NAME logger.handle COMMAND logger_fixture)
    add_test(NAME logger.buffered COMMAND logger_fixture buffered)
    add_test(NAME logger.flush COMMAND logger_fixture flush)
    add_test(NAME logger.partial COMMAND logger_fixture partial)
    add_test(NAME inventory.duplicates COMMAND inventory_fixture)
    add_test(NAME logger.stress COMMAND logger_fixture stress)
    add_test(NAME logger.failure COMMAND logger_fixture failure)
    add_test(NAME logger.zero COMMAND logger_fixture zero)
    add_test(NAME logger.invalid COMMAND logger_fixture invalid)
    add_test(NAME hook.continuous COMMAND hook_fixture continuous)
    add_test(NAME inventory.capacity COMMAND inventory_fixture capacity)
    add_test(NAME inventory.concurrent COMMAND inventory_fixture concurrent)
    add_test(NAME hook.inflight COMMAND hook_fixture inflight)
    add_test(NAME logger.append COMMAND logger_fixture append)
    set_tests_properties(
        hook.guard hook.disabled hook.concurrent hook.continuous hook.inflight dll.wrong-host
        logger.handle logger.buffered logger.flush logger.partial logger.stress logger.failure logger.zero
        logger.invalid logger.append inventory.duplicates inventory.capacity inventory.concurrent
        PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        TIMEOUT 20
        RUN_SERIAL TRUE
    )
endif()

set(AVS_PROJECT_FILES
    src/dll.c src/probe.c src/probe.h src/logger.c src/logger.h src/inventory.c src/inventory.h
    tests/hook_fixture.c tests/loader_fixture.c tests/logger_fixture.c tests/inventory_fixture.c
)
set(AVS_ANALYSIS_FILES src/dll.c src/probe.c src/logger.c src/inventory.c tests/hook_fixture.c tests/loader_fixture.c tests/logger_fixture.c tests/inventory_fixture.c)
find_program(AVS_CLANG_FORMAT NAMES clang-format HINTS "$ENV{ProgramFiles}/LLVM/bin")
find_program(AVS_CLANG_TIDY NAMES clang-tidy HINTS "$ENV{ProgramFiles}/LLVM/bin")

if(AVS_CLANG_FORMAT)
    add_custom_target(format-check
        COMMAND "${AVS_CLANG_FORMAT}" --dry-run --Werror ${AVS_PROJECT_FILES}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM
    )
    add_custom_target(format
        COMMAND "${AVS_CLANG_FORMAT}" -i ${AVS_PROJECT_FILES}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM
    )
else()
    message(STATUS "clang-format not found; set AVS_CLANG_FORMAT to its executable path.")
endif()

if(AVS_CLANG_TIDY)
    # The analysis front end is Clang, but build commands and SDK headers are MinGW.
    set(tidy_args --extra-arg=--target=x86_64-w64-windows-gnu)
    foreach(directory IN LISTS CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES)
        if(NOT directory MATCHES "/lib/gcc/")
            list(APPEND tidy_args "--extra-arg=-isystem${directory}")
        endif()
    endforeach()
    add_custom_target(tidy
        COMMAND "${AVS_CLANG_TIDY}" -p "${CMAKE_BINARY_DIR}" ${tidy_args} ${AVS_ANALYSIS_FILES}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM
    )
else()
    message(STATUS "clang-tidy not found; set AVS_CLANG_TIDY to its executable path.")
endif()
