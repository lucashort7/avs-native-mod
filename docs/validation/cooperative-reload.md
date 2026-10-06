# Cooperative payload reload validation

Validated on October 6, 2026 with native Windows x64 MinGW GCC, CMake and Ninja from WSL. No game injection, commit or push occurred. Earlier runtime-capture documents describe older DLLs, not this architecture.

## Executed checks

From the repository root:

```bat
cmd.exe /d /c build.bat
```

Result: both production DLLs built, 32/32 CTest tests passed in 7.58 seconds. The wrapper exited successfully. With `C:\MinGW\set_distro_paths.bat` called first, these checks also passed:

```bat
cmake --build --preset mingw --target format-check
cmake --build --preset mingw --target tidy
ctest --preset mingw -R bridge.failures --repeat until-fail:10
ctest --preset mingw -R bridge.repeat
```

The invalid-load test passed ten consecutive executions in 0.60 seconds total. The repeated-mapping fixture passed in 0.10 seconds. `git diff --check` passed. Clang-tidy exited zero with no enabled-check findings; its output reported nine warnings suppressed by check filters. This is not a claim of zero compiler-front-end warnings under all check sets.

The complete suite retains the 18 baseline tests, adds 13 bridge tests and one payload lifecycle test. CTest's raw latest run is `build/cmake/Testing/Temporary/LastTest.log`; generated logs and binaries remain ignored.

## What the native fixtures establish

- Real MinHook interception preserves arguments, result pointer, output memory and incoming/outgoing LastError.
- An admitted payload callback blocks cooperative unload. Event barriers control the interleaving, not a sleep-based safety claim.
- Four threads forward 40,000 calls while admission is closed. The paused admitted observer then drains and its real DLL unmaps.
- A thread paused in resident code before acquiring the gate resumes safely after payload unmapping and rechecks admission inside the gate.
- A long original call can remain in flight while the payload unmaps. Its return path belongs to the pinned bridge, not the payload.
- Thirty-two same-process load/unload cycles verify module absence between mappings and 64 original calls across active/inactive observation.
- Sentinel controls load an absolute sibling DLL, consume requests, unload it, replace its binary at the same path and execute generation 2 instead of generation 1.
- Actual extra loader references and an actual module pin prevent verified unmapping and permanently block reload. Dropping the extra reference later does not clear quarantine.
- A real zero-progress logger write causes cleanup refusal. Payload retention and original forwarding are checked.
- Missing/invalid DLLs and nonsibling paths are rejected without breaking original forwarding; a later valid capture works.
- Stale, unsafe and conflicting controls fail closed. Mismatched entry bytes leave the target unchanged. Readiness logs identify the real fixture target while it is still unpatched and the payload absent.
- The production bridge rejects the non-game host without loading the payload. The production payload independently observes, flushes and unmaps.

## Bad Image dialog regression

The resumed baseline timed out after 20.01 seconds in `bridge.failures`. The user captured a `bridge_fixture.exe - Bad Image` dialog naming `invalid-payload.dll`, status `0xc000012f`. That fixture deliberately writes `This is not a PE DLL`. The dialog was a real automation defect, not evidence that the payload callback had crashed.

The fixture now calls `SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS)` before loading the resident controller. This applies only to the fixture process and preserves inherited flags. It does not alter the game, registry or system-wide Windows Error Reporting. The test asserts the mode, rejects the invalid DLL, verifies original forwarding and then runs a valid payload cycle. The bridge records the actual loader error in its log. No error-mode override was added to production DLL code.

Microsoft's SetErrorMode API documentation specifies that SEM_FAILCRITICALERRORS returns critical errors to the process instead of opening the handler dialog, and recommends setting it at application startup to avoid hangs. Reference: `https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode`.

An exact-name Win32_Process query found no remaining `bridge_fixture.exe` instances before rerunning the corrected tests. No process was killed.

## Ownership and unload argument

The pinned bridge owns the hook, trampoline, worker and SRW gate. Payload functions are synchronous and never publish executable work outside their calls. Observation takes a shared gate in resident code, rechecks atomic admission, invokes the payload, then releases the gate before calling the original. Stop closes admission before waiting for the exclusive gate. Once exclusive, all admitted payload calls have returned into resident code; stop flushes and closes the payload, the bridge clears callback pointers, calls FreeLibrary and checks both the exact module path and former image allocation.

This argument does not depend on a delay, a counter stored in unloadable code or the original returning before unload. The bridge never disables/removes the active hook during normal payload reload. Forwarding remains resident. A pre-gate entrant delayed across an entire unload/reload can observe the next generation, but cannot invoke the freed generation through a saved payload pointer.

## Verified artifacts

Both current DLLs have PE machine `0x8664` and characteristics `0x2026`, including the DLL flag.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `build/cmake/avs-bridge.dll` | 349160 | `c9727c382c38b8505945df0c6c6ca56db7745385e680c37ad86fa49f2eede12f` |
| `build/cmake/avs-native-mod.dll` | 320670 | `f05f2e8fee99db8be47549f0e76d340004cd1ff31c457eb9d7e2c1d573b577a4` |
| Preserved historical `build/avs-native-mod.dll` | 345369 | `a9f00cbaa0c661f298207adaf17d7ef8fb6ea05414f6a65a7567c98c068f4c58` |

The historical hash matches the prior worker's pre-change measurement.

## Limits and deployment boundary

No gameplay FPS, live dispatcher ABI, game reload cycle or callback names were validated here. Fixtures use the production bridge source with test-only target/barrier exports; the production bridge's positive game-host branch was not exercised. Historical `probe.c` tests remain baseline coverage, not proof of the new bridge's live behavior.

The contract requires trusted sibling payloads, no competing module loader/unloader and no asynchronous work or recursive target calls from payload operations. This implementation does not contain arbitrary DLL faults. Missing-export, start failure, allocation/event failure, activation failure and every filesystem error are not individually fault-injected by this suite. A callback that never returns can block cooperative stop indefinitely; there is deliberately no forced unload. Before the controller pins the bridge, callers must retain their initial load reference and must not eject it.

Extra references, pins, uncertain cleanup and control/flush failures require process restart. Never force-eject the bridge or load a renamed second copy over its entry. Updating bridge code requires a fresh process. The payload alone is replaceable after verified cooperative unmapping. Live deployment still needs separate approval and a clean game process.
