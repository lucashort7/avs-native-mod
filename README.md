# avs-native-mod

Experimental native C mod for Antivirus Survivors 2003 Professional. The current baseline is a tested observation probe, not a gameplay-changing mod. No GDScript replacements or Godot Mod Loader are required.

## Status

The original sandbox probe intercepted the installed game's dispatcher candidate at RVA 0x58C370. Its one-shot capture logged 64 entries, with three distinct candidate function objects sharing one instance. Automatic stop returned MH_OK and the game remained responsive. See [the runtime evidence](docs/validation/first-runtime-capture.md).

This repository reorganizes that baseline without changing its hook logic. The new artifact and control files have different basenames. Native fixtures and a subsequent approved live capture both passed. The repository DLL logged 64 entries spanning 14 candidate function objects and 14 instances, then stopped interception with MH_OK. See [the repository runtime capture](docs/validation/repository-runtime-capture.md). Do not load it into a process containing another retained probe. Script and function names remain undecoded.

## Layout

- `src/`: DLL lifecycle, dispatcher-entry observation and logger.
- `tests/`: native MinHook integration, forwarding, concurrency, bounded capture and wrong-host loading fixtures.
- `vendor/minhook/`: x64 dependency, upstream license and revision metadata.
- `docs/discovery/`: lifecycle, round flow and dispatcher evidence.
- `docs/validation/`: historical real-game capture and its limits.
- [Next steps](docs/next-steps.md): persistent-handle batched logging and callback identification. These are not implemented yet.

No game executable, PCK, recovered scripts, generated logs or injector binary is distributed here.

## Build and test

Use 64-bit Windows MinGW GCC, CMake 3.23 or newer, and Ninja. Run `build.bat` from cmd.exe or `.\build.bat` from PowerShell. The wrapper prepares MinGW, then configures, builds and tests the `mingw` CMake preset. It uses `AVS_MINGW_SETUP` if set, otherwise `C:\MinGW\set_distro_paths.bat` if present, otherwise GCC on PATH. Put CMake and Ninja on PATH. For another compiler installation, set `AVS_MINGW_SETUP` to its setup script or prepare PATH first.

From WSL, enter the Windows-mounted repository and run `cmd.exe /d /c build.bat`. Linux GCC is not a substitute for this Windows toolchain.

Project-owned targets use `-O2 -Wall -Wextra -Werror`; MinHook also retains the baseline's `-O2` setting. The build produces `build/cmake/avs-native-mod.dll` and runs four CTest fixtures. The separate output directory avoids replacing the earlier DLL loaded from `build/`. It does not inject into the game. Build output and fixture logs stay ignored.

With the MinGW environment prepared, the equivalent commands are:

```bat
cmake --preset mingw
cmake --build --preset mingw
ctest --preset mingw
cmake --build --preset mingw --target format-check
cmake --build --preset mingw --target tidy
```

`format-check` verifies project-owned source and headers without modifying them. `format` applies `.clang-format`. Both exclude `vendor/`. Include sorting is disabled to preserve Win32 header ordering. `tidy` runs a small C static-analysis check set against `build/cmake/compile_commands.json`, with the MinGW target and SDK include paths. It does not change the compiler or apply fixes.

CMake discovers LLVM tools on PATH or under `%ProgramFiles%\LLVM\bin`. Override the CMake cache options with `-D`, for example `cmake --preset mingw -DAVS_CLANG_TIDY="C:/path/clang-tidy.exe"`; `AVS_CLANG_FORMAT` works the same way. These are not environment-variable overrides. If either tool is unavailable, CMake reports that its optional target was not added; the native build and tests remain usable. No hosted CI is configured yet.

## Controls and safety

The worker accepts only `AVS03Pro.exe` matching the recorded PE timestamp, image size and entry bytes. Unexpected builds refuse preparation rather than guessing compatibility after an update.

After an approved injection, preparation leaves the hook DISABLED. A newly created `avs-native-mod.enable` file beside the DLL requests one capture. `avs-native-mod.stop` cancels waiting or requests interception stop. Existing control files at startup cause refusal. The passive wait expires after 300 seconds.

The capture stores at most 64 entry samples and requests stop after that limit or its bounded polling window. The detour records opaque pointer values and argument count in preallocated memory. The worker writes them to disk. No unverified game object layout is dereferenced.

The DLL pins itself until process exit. After any activation attempt it retains the trampoline too. Stop does not cancel in-flight calls. Do not eject, hot-replace or inject another copy over the same entry. Restart the game before another deployment.

The current logger still opens and closes its file per written line. Continuous capture and batched persistent-handle logging are planned, not implemented. Process responsiveness and a successful short capture do not establish continuous-logging FPS.

## License

Project code is MIT, as specified in [LICENSE](LICENSE). MinHook and its disassembler retain their own notices under `vendor/minhook/`. This project does not claim ownership of the game or redistribute its resources.