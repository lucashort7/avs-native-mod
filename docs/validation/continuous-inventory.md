# Continuous inventory fixture validation

Validated on 2026-10-06 using native Windows x64 MinGW, CMake and Ninja. This is fixture evidence, not a new game capture.

## Results

- `build.bat` passed all 18 CTest cases. `format-check`, `tidy` and `git diff --check` passed.
- Native MinHook forwarding preserved all seven machine-level arguments, return storage, return value and LastError. The concurrent fixture intercepted 513 calls and wrote one first-seen row.
- The continuous worker stayed active beyond the old five-second window and the 64-call sample limit. It flushed the actual file on its 2000 ms interval and consumed the stop sentinel before returning.
- The in-flight fixture intercepted 514 calls. One original call remained paused while the main thread disabled interception, froze metadata, drained the inventory and closed the log. The original then resumed with unchanged forwarding and return behavior. The final file contained one first-seen row, one function-total row with 514 calls, and `observed=514 unique=1 untracked_calls=0`.
- The standalone inventory fixture counted 8000 concurrent hits as one function. An eight-slot test build filled its table, accounted for one untracked call and kept counting admitted functions.
- The standalone logger fixture verified a persistent handle, deferred byte-buffer writes, explicit flush, final drain, append across reopening, 800 concurrent records, an oversized bounded line and complete CRLF records read from the actual file.
- A fixture-only writer adapter exercised partial writes, failure after a seven-byte prefix and zero-progress writes. Partial writes completed; failures stayed sticky, did not duplicate a written prefix, preserved LastError and closed the handle. Production builds do not include this adapter.
- Invalid logger paths and initialization inputs failed without fallback file creation.

## Artifact

- DLL: `build/cmake/avs-native-mod.dll`, verified PE AMD64 with the DLL characteristic.
- SHA-256: `a45c2f276296dc3b4a2425017d4fba16fd72eddb8265fd6d5d3319f9f557d731`.
- The earlier `build/avs-native-mod.dll` stayed unchanged. No injection, activation or ejection occurred during this work.
- Fixture output and logs remain ignored by Git. Rebuilding changes artifact identity; revalidate the chosen binary before any deployment.

## Limits

The production inventory holds at most 4096 keys with a 64-probe lookup budget. The inventory itself is the bounded pending-first-seen buffer; it does not need a second queue containing the same records. Unadmitted calls increment `untracked_calls`, not a missing-unique-functions estimate. Repeated calls update counters and do not enqueue more lines.

Raw pointers remain provisional capture-scoped identities. Reuse can merge different objects, names remain undecoded, and final function totals do not preserve individual lifecycle events. Worker timestamps describe formatting time. Metadata and byte-buffer content can be lost on forced process exit.

The native fixtures establish implementation behavior. They do not confirm the candidate engine symbol, the game ABI, continuous-mode FPS or lifecycle attribution. Live deployment still needs a clean process and separate approval.
