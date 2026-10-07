# avs-native-mod

Experimental native C mod for Antivirus Survivors 2003 Professional. The current code observes a continuous inventory of candidate script functions. A one-shot, filtered Coin experiment also changes a live argument and can affect gameplay. No GDScript replacements or Godot Mod Loader are required.

## Status

The original sandbox probe intercepted the installed game's dispatcher candidate at RVA 0x58C370. Its one-shot capture logged 64 entries, with three distinct candidate function objects sharing one instance. Automatic stop returned MH_OK and the game remained responsive. See [the runtime evidence](docs/validation/first-runtime-capture.md).

A later short-capture repository DLL logged 64 entries spanning 14 candidate function objects and 14 instances, then stopped interception with MH_OK. See [the repository runtime capture](docs/validation/repository-runtime-capture.md). These live results belong to the earlier bounded probe, not the new continuous mode.

The current build splits the permanent hook owner, `avs-bridge.dll`, from the reloadable observer, `avs-native-mod.dll`. Native fixtures verify cooperative payload unmapping and replacement without unloading the bridge. See [hot-reload validation](docs/validation/cooperative-reload.md). Live captures on October 6 and 7 verified this lifecycle in the game. Do not load it into a process containing another retained probe. The payload now snapshots the script path and function name on the first observed call to each function object.

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

Project-owned targets use `-O2 -Wall -Wextra -Werror`; MinHook also retains the baseline's `-O2` setting. The build produces `build/cmake/avs-bridge.dll` and `build/cmake/avs-native-mod.dll` and runs 32 CTest fixtures. The separate output directory avoids replacing the earlier DLL loaded from `build/`. It does not inject into the game. Build output and fixture logs stay ignored. `src/probe.c` is the historical retention implementation, linked only into the legacy fixture, not either production DLL.

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

The Windows PowerShell helper `avs-lifecycle.ps1` offers `list`, `inject` and `unload` for the local development layout. It finds `win32-hotswap-dll/build/hotswap.exe` in the workspace root and requires exactly one running game process. From this directory in PowerShell, run `.\avs-lifecycle.ps1 list` to check both module paths, `.\avs-lifecycle.ps1 inject` to load only the bridge, or `.\avs-lifecycle.ps1 unload` to request cooperative payload removal. From WSL, run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\obsidian\antivirus-survivors-modding\avs-native-mod\avs-lifecycle.ps1' list`. The script checks a fresh disabled-ready log after injection and checks the cleanup log plus module absence after unloading. It never ejects the pinned bridge or enables observation; `.enable` is a separate action.

The worker accepts only `AVS03Pro.exe` matching the recorded PE timestamp, image size and entry bytes. Unexpected builds refuse preparation rather than guessing compatibility after an update.

After separately approved deployment of the bridge, preparation leaves the hook DISABLED and the payload absent. Keep both DLLs in the same directory. A newly created regular `avs-native-mod.enable` file beside the bridge loads the sibling payload and starts observation. `avs-native-mod.unload` stops and unloads the payload; `.stop` is an alias. The resident worker consumes each command file. Existing controls at startup, conflicting commands, directories and reparse-point controls cause refusal. The worker waits until process exit, not for a 300-second capture window.

Once enabled, capture runs until `.unload`, `.stop`, a logger/control error or process exit. There is no five-second or 64-entry limit. After successful unload, verify the bridge log says `success=1 unmapped=1 reload_allowed=1` and independently confirm the exact payload module is absent before replacing its file. Create a fresh `.enable` only after replacement. Never use injector eject as this protocol.

The detour updates a fixed in-memory inventory keyed only by `function_object`. On the first call it copies the instance, argument count, opaque pointers, and bounded UTF-32LE script/function labels from the AVS function-object layout verified on this installed build. Reads use `ReadProcessMemory` against the current process and mark failures `<unreadable>`; later calls only increment the counter. Labels are copied before the first-seen row is flushed, so an object freed afterward does not change that row. Pointer reuse within a capture can still merge unrelated functions; keys are provisional and never persist across processes. A narrow exception snapshots the first 16 calls to `res://scenes/ui/game_currency_ui.gd → on_currency_collected`: with `argc=2`, it copies 16 bytes from each pointed-to argument while the callback is live. `arg_sample` rows include the raw 32-bit type and 64-bit payload; boolean, integer and floating-point scalars are decoded provisionally, and other types remain `<unsupported>`. An unreadable pointer becomes `<unreadable>`; the assumed Variant layout has not yet been corroborated against AVS values. The log is flushed later, but never dereferences a saved argument address later. No extra callbacks are logged after the 16-record cap in one payload mapping.

The one-shot intervention is restricted to `res://scenes/autoload/game_events.gd → emit_currency_collected`. Pickup calls may pass only `type` and `amount` (`argc=2`); callers that explicitly supply the optional flags pass four arguments, in which case the fixture validates both boolean Variants. It checks the first Variant's UTF-32 `Coin` string and the second's floating-point type and finite positive amount at most 1000 before replacing the amount's eight data bytes with ten times its original value. It attempts at most one matching write per payload load, and emits `coin_x10 target=game_events.emit_currency_collected original=... multiplied=... success=...` at the next flush. It does not change the resident bridge. This deliberately writes the supplied Variant in place before signal dispatch, so connected handlers may all observe the modified amount. The native fixture proves a one-shot emitter write, manager-handler bypass and non-Coin bypass, not that the live game's balance changes as expected. After a live test, unload the payload cooperatively; re-enabling it resets the one-shot guard.

The resident bridge worker calls the payload flush operation every 2000 ms. The payload owns its logger and inventory but starts no threads. It writes `first_seen` rows once, then `function_delta interval=N function_object=... calls_in_interval=M` for admitted functions with new calls since their previous flush; final `function_total` and inventory totals remain. Interval numbers start at 1 per payload mapping and advance on each flush, including empty flushes. The stop flush can be shorter than two seconds. Snapshot boundaries are per-row reads during the flush, not atomic across all functions; concurrent calls may fall on either adjacent interval. At most 4096 delta rows are attempted per flush. A failed log write/flush is sticky and stops later output; a partially written interval must not be interpreted as complete. It uses an 8 KiB byte buffer and checks complete writes, including partial writes. A full byte buffer flushes earlier. Log timestamps are local formatting times at flush, not exact callback entry times. The observation callback neither allocates memory nor waits for disk I/O.

For phase correlation without changing the pinned bridge, the operator can append external markers to `build/cmake/phase-markers.log` on each user "now" message, using a timestamp with timezone and a phase label. For example, in PowerShell from the repository's `avs-native-mod` directory: `"$((Get-Date).ToString('o')) phase=run-start" | Add-Content -LiteralPath build/cmake/phase-markers.log`. Repeat for menu, death, pause and return-to-menu; keep one marker per user signal and record receipt time, not a backdated game event. Compare marker timestamps to the payload log's local timestamps and interval IDs; each delta aggregates calls since the preceding flush (normally about two seconds), so boundary intervals can straddle a marker and cannot establish exact event ordering. This file is external metadata, not read by the DLL or bridge. Avoid relying on timestamps alone across timezone/clock changes; retain interval order and label ambiguous boundaries.

`investigation/probe_function_names.py --pid <current-PID> --log build/cmake/avs-native-mod.log` reads only the latest `capture start` section, up to 4096 deduplicated `first_seen` keys. It reports decoded names and an activity-ranked candidate list from delta rows. Run while that same capture and process are still alive; check PID and capture start against the current run. A pointer is only a provisional capture-scoped key: the object may have been freed/reused even in the same process, so successful reads and plausible names are candidates, never proof of historical identity. Rejected/unreadable objects remain reported. Do not decode an earlier capture against a later process or infer event names from raw call volume alone.

The table holds at most 4096 functions and bounds lookup to 64 probes. Unadmitted calls increment `untracked_calls`; this is not a count of distinct missing functions. Saturation does not stop capture, and admitted functions keep their counters. On stop, the bridge closes observation admission, waits for admitted callbacks, and asks the payload to freeze metadata, drain pending rows, write totals and close its logger. Repeated lifecycle events need a separate occurrence-sensitive mode.

Only the bridge pins itself. It owns MinHook, the detour, trampoline, worker and callback gate until process exit. After the first enable, the hook remains installed even with no payload and forwards the original unchanged. The shared gate covers only the payload callback and is released before calling the original. Unload closes admission before acquiring the exclusive gate, then calls stop and `FreeLibrary` from resident code. It checks the exact module path and former image allocation before allowing reload. An extra reference, pin or cleanup failure permanently blocks replacement. Do not eject or replace the bridge, load a renamed second bridge, or install another hook over its entry. Changing bridge code requires a fresh process.

Payload output is `avs-native-mod.log`; resident lifecycle output is `avs-bridge.log`, both beside the bridge. Generated logs stay ignored by Git. Forced process exit can lose buffered data. A checked write failure stops observation with error status instead of retrying a potentially written prefix. Payload implementations must obey the synchronous contract in `src/payload.h`: no escaping threads, timers, callbacks, executable pointers or recursive target calls. This is cooperative unloading, not a sandbox for arbitrary DLLs.

The bridge's `script_probe_status` export returns 1 when prepared and disabled, 7 during capture, 2 after successful payload stop/unmap, 3 for wrong-host refusal, 4 for terminal failure and 8 for recoverable payload-load rejection. Codes 5 and 6 belong to the historical implementation. These are application status codes, not Win32 errors. Continuous-mode gameplay FPS is unmeasured.

## License

Project code is MIT, as specified in [LICENSE](LICENSE). MinHook and its disassembler retain their own notices under `vendor/minhook/`. This project does not claim ownership of the game or redistribute its resources.