# Next steps

The cooperative bridge/payload architecture passed native fixtures. See validation/cooperative-reload.md for commands, coverage and limits. No bridge/payload game capture has happened yet.

## Next: validate continuous capture in the game

Review the resident bridge and synchronous payload contract together before deployment. Use a fresh process without retained probe modules and obtain separate approval. Confirm both reviewed DLLs, current PID, PE identity and the bridge's disabled-ready log before enabling. Check first-seen rows, saturation and stop totals against the player's screen or phase. Verify actual payload module absence before replacing it. Never eject or replace the bridge. Measure gameplay FPS; fixture results and earlier short captures do not establish game performance or ABI correctness.

## Then identify callbacks

Validate the installed build's function-object and StringName layouts before reading names. Correlate decoded script/function pairs with controlled pause, resume, menu, round and Safe Folder transitions. Record evidence separately from inferred meaning. Captured heap pointers are scoped to that process and object lifetime.

## Build and tooling baseline

CMake with Ninja builds the native Windows x64 MinGW targets and runs 32 fixtures through CTest. Output and the compilation database live under build/cmake, separate from the earlier live-test DLL. Add another compiler or hosted CI only when the same native fixture suite can run there. Keep third-party notices, exclude game resources, and never ship the local injector as part of this baseline.
