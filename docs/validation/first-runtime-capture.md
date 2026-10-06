# First AVS dispatcher-entry capture

Lucas approved injection and one short capture with MARTELO on 2026-10-06. No game files were replaced and no script overrides were installed.

## Artifact and target

- DLL SHA-256: 3f0ca501a7bc63495e3b7dff20dba38d09f5045e42a74bb077876f5d1b37407e.
- Process: AVS03Pro.exe, PID 9412, installation under C:\Program Files (x86)\Steam\steamapps\common\Antivirus Survivors 2003 Professional.
- DLL: C:\obsidian\antivirus-survivors-modding\avs-mod\script-probe\avs-script-probe.dll.
- Prepared target: 00007ff62b62c370, RVA 0x58C370.
- Injector: existing win32-hotswap-dll/src/hotswap.c compiled without source changes into hotswap-probe.exe in this folder. Its CLI accepts an executable name, not a PID. Unique process identity, module absence, artifact hash and control-file absence were checked immediately before injection.

## Read-back evidence

The full DLL path was independently observed in PID 9412. Fresh log records at 14:11:36 showed attachment, initialization MH_OK, disabled creation MH_OK and the expected target. Only after that read-back was the approved .enable file created.

At 14:11:50, the log recorded enable MH_OK, entries 1 through 64, stop MH_OK and capture end with stopped=1, logged=64 and observed_at_report=1053. A parser verified the complete entry sequence and counted three distinct candidate function-object pointers and one instance pointer.

| Candidate function object | Logged entries | Argument count |
| --- | ---: | ---: |
| 0000021e1348c1d0 | 22 | 1 |
| 0000021e1348c540 | 21 | 1 |
| 0000021e1348b410 | 21 | 0 |

Every sample used instance 0000021e1166cdf0 and a null suspended-state pointer. The first entry recorded function_object=0000021e1348c1d0, instance=0000021e1166cdf0 and argc=1. These are historical addresses for this process, not reusable locators.

After automatic stop, PID 9412 was alive and Responding=true, the exact DLL remained loaded, and the .enable file was absent. Responsiveness does not measure FPS or independently prove every original call returned. The native fixtures separately verified unchanged forwarding and return behavior. Active interception has ended; DLL and trampoline remain until process exit. Do not eject or reinject them.

## What remains unknown

The entry is now proven hookable in this game instance, with real callback records. The identification as GDScriptFunction::call remains strongly supported but not symbol-confirmed. The samples do not identify script names or functions, establish object layouts, or correlate these callbacks with pause, round start or a menu transition. No live function-object dereference was attempted.

Raw evidence is in avs-script-probe.log, with the game attachment starting at line 5. The previous four lines belong to wrong-host loader fixtures and must not be counted as game callbacks.
