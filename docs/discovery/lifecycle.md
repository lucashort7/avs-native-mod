# AVS lifecycle entry points from packaged scripts

## Scope

Static findings from the installed AVS03Pro.pck. The package header reports format 4 and engine 4.7.1. Target script bytes were copied without changing the game installation and checked against the package entry MD5. GDRE Tools v2.7.0 successfully decompiled pause_menu.gdc and game_events.gdc with --bytecode=4.7.1; the tool selected bytecode revision ebc36a7, labeled 4.5.0-stable. No hooks or runtime transitions were tested.

## Evidence

Paths below are relative to this investigation directory.

- pause_menu/decompiled/pause_menu.gd:27-28: _ready calls GameEvents.pause_game(0.0).
- pause_menu/decompiled/pause_menu.gd:73-93: close awaits its animation tween, restores the cursor, calls GameEvents.unpause_game(0.0), then queues the menu for deletion.
- pause_menu/decompiled/pause_menu.gd:104-110: the exit callback calls GameEvents.return_to_main_menu().
- game_events/decompiled/game_events.gd:329-350: pause_game sets pausing, optionally awaits a slowdown tween, sets SceneTree.paused true, disables main and foreground processing, and emits paused_handled. This function is also called by the fullscreen queue, so it is not specific to the pause menu.
- game_events/decompiled/game_events.gd:353-372: unpause_game rejects return/abort states, unpauses the tree, restores processing, optionally awaits a speedup tween, clears pausing, and emits unpause_handled.
- game_events/decompiled/game_events.gd:422-432: emit_open_main emits open_main BEFORE changing to scenes/main/main.tscn. It then awaits scene_changed before resolving the main and layer nodes. The signal is a request-side event, not proof that gameplay is ready.
- game_events/decompiled/game_events.gd:583-601: end_run clears effects, pools, protocol and node references. It is cleanup, not yet a verified result-screen event.
- game_events/decompiled/game_events.gd:612-672: return_to_main_menu guards duplicate requests and starts an async transition. The async path calls StatTracker.finish_run(false), saves progression, aborts overlays, clears the run, calls StatsManager.initialize_stats, changes to the menu, awaits scene_changed, then restores unpaused state.
- game_events/decompiled/game_events.gd:375-382: roll_rarity reads StatsManager.final_stats during its calculation. StatsManager is therefore referenced outside the end-run report path too; its exact ownership of Coin/Keys/Beanz remains unresolved.

## Next discriminating step

Inspect the caller of emit_open_main and the main scene script to establish when a run becomes ready. Distinguish entering the gameplay scene from a later stage transition. The package/script route replaces a blind generic-copy search for naming gameplay functions; native addresses and an actual mod access mechanism remain unverified.
