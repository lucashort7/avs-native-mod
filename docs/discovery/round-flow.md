# AVS preparation and round transitions

## Verified static artifacts

GDRE Tools v2.7.0 extracted exactly two selected package entries and successfully decompiled both using --bytecode=4.7.1. Its extraction run reported no checksum errors. These findings describe recovered scripts, not runtime hook tests. The installed game files and process were not modified.

## Preparation

The previously observed initial weapon-selection screen is implemented by starter_folder.gd:119, whose prompt matches the screenshot. The script adds itself to the starter_folder group at line 41 and tracks on_weapon_screen at line 37. RoundManager._ready connects gameplay events, awaits a frame, and calls on_starter_folder at round_manager.gd:130-148. That function clears the existing level, installs STARTER_FOLDER, marks a safe area and unblocks the transition at lines 731-757.

## Start a battle round

starter_folder.gd:356-360 handles the start button and emits GameEvents.battle_folder_opened. RoundManager connects that signal at line 133. Its handler at lines 760-777 clears the previous level, awaits level_cleared, invokes new_battle_level, increments current_battle_round, records maxima and resets round counters.

new_battle_level at round_manager.gd:235-272 generates the battle map and contains awaits. It starts enemy spawning and arena timers before awaiting the unblock animation. The handler calls it without await, so completion of on_battle_folder_opened does not by itself prove that the battlefield is ready. Distinguish the transition request, counter increment, timer start and visual readiness.

## Round deadline and Safe Folder

on_arena_timer_finished at lines 523-527 normally spawns the Safe Folder objective; in the fifth round it instead starts the boss-choice path unless the boss phase is already active or its boss has been defeated. spawn_end_objective emits safe_folder_spawned at lines 541-542 when spawning SAFE_FOLDER. A timed objective starts FailTimer at lines 556-560. The deadline callback at lines 911-917 opens a BlueScreen overlay on failure. Timer expiry is not entry into the safe hub.

on_safe_folder_opened at lines 837-862 stops the deadline, credits Beanz, evaluates unlocks, snapshots last-round counters and saves progression. For rounds below five it clears the arena and invokes new_safe_level; otherwise it calls StatTracker.finish_run(true) and opens EndScreen. new_safe_level at lines 275-296 installs SAFE_FOLDER_HUB and changes the player to a safe area.

## Counters

RoundManager declares live-round and last-round Coin/Keys/Beanz counters at lines 78-86. on_currency_collected at lines 875-884 ignores nonpositive or excluded amounts and accumulates live-round counters. reset_round_stats at lines 893-899 zeros them. snapshot_last_round at lines 902-908 copies them into the report snapshot. This explains separate round/snapshot candidates but does not identify the previously watched accumulated-run heap block.

## Next boundary

Static script names and transition logic are now available. Native hook placement still requires locating the dispatcher and validating function identity and calling convention against the live executable. No GDScript replacement, mod-loader installation, debugger attach or DLL injection was performed.
