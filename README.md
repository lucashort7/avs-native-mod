# avs-native-mod

Experimental native C mod for Antivirus Survivors 2003 Professional. The current code observes a continuous inventory of candidate script functions. It does not change gameplay. No GDScript replacements or Godot Mod Loader are required.

## Status

The original sandbox probe intercepted the installed game's dispatcher candidate at RVA 0x58C370. Its one-shot capture logged 64 entries, with three distinct candidate function objects sharing one instance. Automatic stop returned MH_OK and the game remained responsive. See [the runtime evidence](docs/validation/first-runtime-capture.md).

A later short-capture repository DLL logged 64 entries spanning 14 candidate function objects and 14 instances, then stopped interception with MH_OK. See [the repository runtime capture](docs/validation/repository-runtime-capture.md). These live results belong to the earlier bounded probe, not the new continuous mode.

The continuous inventory passed 18 native fixtures, format-check and clang-tidy. See [the fixture validation](docs/validation/continuous-inventory.md). It has not been deployed to the game yet. Do not load it into a process containing another retained probe. Script and function names remain undecoded.

## Layout

- `src/`: DLL lifecycle, dispatcher-entry observation and logger.
- `tests/`: native MinHook integration, forwarding, continuous capture, deduplication, file logging and wrong-host loading fixtures.
- `vendor/minhook/`: x64 dependency, upstream license and revision metadata.
- `docs/discovery/`: lifecycle, round flow and dispatcher evidence.
- `docs/validation/`: historical real-game capture and its limits.
- [Next steps](docs/next-steps.md): live continuous-capture validation and callback identification.

No game executable, PCK, recovered scripts, generated logs or injector binary is distributed here.

## Build and test

Use 64-bit Windows MinGW GCC, CMake 3.23 or newer, and Ninja. Run `build.bat` from cmd.exe or `.\build.bat` from PowerShell. The wrapper prepares MinGW, then configures, builds and tests the `mingw` CMake preset. It uses `AVS_MINGW_SETUP` if set, otherwise `C:\MinGW\set_distro_paths.bat` if present, otherwise GCC on PATH. Put CMake and Ninja on PATH. For another compiler installation, set `AVS_MINGW_SETUP` to its setup script or prepare PATH first.

From WSL, enter the Windows-mounted repository and run `cmd.exe /d /c build.bat`. Linux GCC is not a substitute for this Windows toolchain.

Project-owned targets use `-O2 -Wall -Wextra -Werror`; MinHook also retains the baseline's `-O2` setting. The build produces `build/cmake/avs-native-mod.dll` and runs 18 CTest fixtures. The separate output directory avoids replacing the earlier DLL loaded from `build/`. It does not inject into the game. Build output and fixture logs stay ignored.

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

Once enabled, capture runs until `.stop`, a logger/control error or process exit. The earlier five-second and 64-entry stop conditions are gone. The first 64 raw samples remain an internal fixture diagnostic, not a duration limit.

The detour updates a fixed in-memory inventory keyed only by `function_object`. It records the first instance, argument count and opaque pointers, then counts repeated calls without emitting another first-seen row. No unverified game object layout is dereferenced. Pointer reuse can merge unrelated functions, so keys are provisional and never persist across processes.

The DLL worker owns one open log file and scans for new inventory rows every 2000 ms. It writes `first_seen` rows once, uses an 8 KiB byte buffer and checks complete writes, including partial writes. A full byte buffer flushes earlier. Log timestamps are worker formatting times, not exact callback entry times. The detour neither allocates memory nor waits for disk I/O.

The table holds at most 4096 functions and bounds lookup to 64 probes. Unadmitted calls increment `untracked_calls`; this is not a count of distinct missing functions. Saturation does not stop capture, and admitted functions keep their counters. On stop, the worker disables the hook, freezes metadata publication, drains pending first-seen rows and writes one `function_total` row per function plus inventory totals. Repeated lifecycle events need a separate occurrence-sensitive mode.

The DLL pins itself until process exit. After any activation attempt it retains the trampoline too. Stop does not cancel in-flight calls. Do not eject, hot-replace or inject another copy over the same entry. Restart the game before another deployment.

The local append-only log is `build/cmake/avs-native-mod.log`, beside the new DLL. Generated logs stay ignored by Git. Forced process exit can lose metadata or lines not yet flushed. A checked write failure stops capture with error status instead of retrying a potentially written prefix.

The `script_probe_status` export returns 1 when prepared and disabled, 7 during continuous capture, 2 after successful stop, 3 for wrong-host refusal, 4 for an error, 5 for passive-wait expiry and 6 for pre-activation cancellation. These are application status codes, not Win32 errors. Continuous-mode gameplay FPS is unmeasured.

## License

Project code is MIT, as specified in [LICENSE](LICENSE). MinHook and its disassembler retain their own notices under `vendor/minhook/`. This project does not claim ownership of the game or redistribute its resources.