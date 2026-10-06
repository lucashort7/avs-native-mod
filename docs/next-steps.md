# Next steps

The continuous inventory and persistent-handle batched logger passed native fixtures. No continuous-mode game capture has happened yet.

## Next: validate continuous capture in the game

Use a fresh process without retained probe modules and obtain separate deployment approval. Confirm the reviewed DLL, current PID, PE identity and disabled-ready log before enabling. Check first-seen rows, table saturation and stop totals against the actual screen or phase reported by the player. Measure capture overhead and ask about gameplay FPS; the earlier short captures do not establish continuous-capture performance. Never eject or replace a retained active-hook DLL.

## Then identify callbacks

Validate the installed build's function-object and StringName layouts before reading names. Correlate decoded script/function pairs with controlled pause, resume, menu, round and Safe Folder transitions. Record evidence separately from inferred meaning. Captured heap pointers are scoped to that process and object lifetime.

## Build and tooling baseline

CMake with Ninja builds the native Windows x64 MinGW targets and runs 18 fixtures through CTest. LLVM format and tidy passed against project-owned files. Output and the compilation database live under build/cmake, separate from the earlier live-test DLL. Add another compiler or hosted CI only when the same native fixture suite can run there. Keep third-party notices, exclude game resources, and never ship the local injector as part of this baseline.
