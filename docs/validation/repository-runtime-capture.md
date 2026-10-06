# Repository DLL runtime capture

On 2026-10-06 Lucas approved injection and one bounded capture after restarting the game. The checked process was AVS03Pro.exe, PID 2236, with no prior sandbox module loaded.

## Artifact

- Repository baseline commit: 960bd513dc548202889cd1a32c5323c90917bb21.
- Built artifact: build/avs-native-mod.dll.
- SHA-256: a9f00cbaa0c661f298207adaf17d7ef8fb6ea05414f6a65a7567c98c068f4c58.
- Windows DLL path: C:\obsidian\antivirus-survivors-modding\avs-native-mod\build\avs-native-mod.dll.
- Runtime target: 00007ff62b62c370, RVA 0x58C370.

The rebuild passed all native fixtures, including 513 intercepted calls with concurrent producers, and its PE architecture and DLL flag were checked. Process identity, artifact hash, absence of earlier sandbox modules and absence of control flags were reconfirmed immediately before injection.

## Evidence

The full module path was read back from PID 2236. The fresh attachment block began at line 5 of build/avs-native-mod.log. Initialization and disabled creation both returned MH_OK before the enable request was created. Enable returned MH_OK, and the request file was consumed.

The current-PID block contains 64 sequential samples, 14 distinct candidate function-object pointers and 14 distinct instance pointers. The worker reported observed_at_report=408, followed by stopped=1 and logged=64. Stop returned MH_OK. Sample count and observed-at-report count are different measures.

First sample: function_object=000001999f611750, instance=000001999f7b9ef0, argc=1.

After capture, PID 2236 was alive and Responding=true. The exact DLL remained mapped and the enable request was absent. Interception stopped automatically. DLL and trampoline remain until process exit; do not eject or reinject them. No additional function-object or StringName memory reads were performed.

These records validate the reorganized DLL's live interception and bounded capture, not engine symbol identity, callback names, full object layouts or correlation with a lifecycle event. No current screen or gameplay state was inferred from the captured pointers. Player-observed FPS still needs separate confirmation.
