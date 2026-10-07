# Next steps

The cooperative bridge/payload architecture passed native fixtures and ran in AVS03Pro on October 6 and 7, 2026. See validation/cooperative-reload.md for fixture commands and limits; the October 6 full-run evidence is recorded in the shared AVS research notes. On October 7, two short captures tested first-call script/function labels from the C payload. The first recorded 527 labeled objects, 257,567 calls and zero untracked calls; the second recorded 427 labeled objects, 82,118 calls and zero untracked calls. No label was unreadable in either run. Each payload cooperatively unmapped while the bridge remained in the game.

## Next: validate labeling across game phases

With the existing bridge, enable the reloadable payload for a controlled menu, pause and resume sequence. Confirm fresh named `first_seen` rows against the screen and recovered scripts, and ask the player about FPS during the first-call burst. The logger's timestamp is the flush time, not call entry. Verify payload absence before replacing it; never eject or replace the bridge in this process. Pointer reuse may still merge different function objects that occupy the same address later in one capture.

## Then identify the Coin producer

The macro-transition methods have been correlated with a marked full run. The accumulator owner and the arithmetic producer of one Coin pickup remain unverified. Use a controlled pickup and a targeted probe rather than attributing generic script-call volume to Coin arithmetic.

## Build and tooling baseline

CMake with Ninja builds the native Windows x64 MinGW targets and runs 32 fixtures through CTest. Output and the compilation database live under build/cmake, separate from the earlier live-test DLL. Add another compiler or hosted CI only when the same native fixture suite can run there. Keep third-party notices, exclude game resources, and never ship the local injector as part of this baseline.
