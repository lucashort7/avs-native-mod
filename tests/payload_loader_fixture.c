#include <windows.h>
#include "probe.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void utf32_ascii(uint32_t *out, const char *text) {
    do {
        *out++ = (unsigned char)*text;
    } while (*text++);
}

int main(void) {
    DeleteFileW(L"payload-fixture.log");
    HMODULE module = LoadLibraryW(L".\\avs-native-mod.dll");
    if (!module)
        return 1;
    union {
        FARPROC raw;
        BOOL(WINAPI *fn)(const wchar_t *, const wchar_t *);
    } start = {.raw = GetProcAddress(module, "avs_payload_start")};
    union {
        FARPROC raw;
        void(WINAPI *fn)(const probe_sample *);
    } observe = {.raw = GetProcAddress(module, "avs_payload_observe")};
    union {
        FARPROC raw;
        BOOL(WINAPI *fn)(void);
    } flush = {.raw = GetProcAddress(module, "avs_payload_flush")};
    union {
        FARPROC raw;
        BOOL(WINAPI *fn)(void);
    } stop = {.raw = GetProcAddress(module, "avs_payload_stop")};
    if (!start.raw || !observe.raw || !flush.raw || !stop.raw) {
        fputs("FAIL: reloadable payload lifecycle exports missing\n", stderr);
        return 1;
    }
    int token;
    probe_sample sample = {.function = &token, .count = 3};
    SetLastError(0x1234);
    if (!start.fn(L".\\", L"payload-fixture"))
        return 1;
    observe.fn(&sample);
    observe.fn(&sample);
    if (!flush.fn())
        return 1;
    observe.fn(&sample);
    if (!flush.fn())
        return 1;
    /* Model the validated AVS object/string layout. The name must be copied
     * on first observation, not read after the source memory changes. */
    uint32_t name[64], source[96];
    uintptr_t name_data[2] = {0, (uintptr_t)name};
    uintptr_t source_data[2] = {0, (uintptr_t)source};
    uintptr_t function[0x2d8 / sizeof(uintptr_t) + 1] = {0};
    utf32_ascii(name, "_on_resume_button_clicked");
    utf32_ascii(source, "res://scenes/ui/pause_menu.gd");
    function[0] = (uintptr_t)name_data;
    function[1] = (uintptr_t)source_data;
    function[0x2d8 / sizeof(uintptr_t)] = (uintptr_t)&function;
    probe_sample named = {.function = function, .count = 0};
    observe.fn(&named);
    name[0] = 'X';
    source[0] = 'X';
    observe.fn(&named);
    /* Two calls to one named UI handler must copy both arguments at entry,
     * not read the first call's transient pointers during a later flush. */
    uint32_t ui_name[64], ui_source[96];
    uintptr_t ui_name_data[2] = {0, (uintptr_t)ui_name};
    uintptr_t ui_source_data[2] = {0, (uintptr_t)ui_source};
    uintptr_t ui_function[0x2d8 / sizeof(uintptr_t) + 1] = {0};
    utf32_ascii(ui_name, "on_currency_collected");
    utf32_ascii(ui_source, "res://scenes/ui/game_currency_ui.gd");
    ui_function[0] = (uintptr_t)ui_name_data;
    ui_function[1] = (uintptr_t)ui_source_data;
    ui_function[0x2d8 / sizeof(uintptr_t)] = (uintptr_t)&ui_function;
    uint64_t first[2] = {2, 123}, second[2] = {3, 0};
    double one_half = 1.5;
    memcpy(&second[1], &one_half, sizeof one_half);
    const void *arguments[2] = {first, second};
    probe_sample ui = {.function = ui_function, .args = arguments, .count = 2};
    observe.fn(&ui);
    first[1] = 456;
    observe.fn(&ui);
    first[1] = 789;
    if (!flush.fn())
        return 1;
    /* The manager-side Coin call must pass unchanged. The upstream GameEvents
     * emitter is the one-shot experiment target. */
    uint32_t manager_name[64], manager_source[96], emitter_name[64], emitter_source[96], coin[8], keys[8];
    uintptr_t manager_name_data[2] = {0, (uintptr_t)manager_name};
    uintptr_t manager_source_data[2] = {0, (uintptr_t)manager_source};
    uintptr_t manager_function[0x2d8 / sizeof(uintptr_t) + 1] = {0};
    uintptr_t emitter_name_data[2] = {0, (uintptr_t)emitter_name};
    uintptr_t emitter_source_data[2] = {0, (uintptr_t)emitter_source};
    uintptr_t emitter_function[0x2d8 / sizeof(uintptr_t) + 1] = {0};
    utf32_ascii(manager_name, "on_currency_collected");
    utf32_ascii(manager_source, "res://scenes/manager/game_currency_manager.gd");
    utf32_ascii(emitter_name, "emit_currency_collected");
    utf32_ascii(emitter_source, "res://scenes/autoload/game_events.gd");
    utf32_ascii(coin, "Coin");
    utf32_ascii(keys, "Keys");
    manager_function[0] = (uintptr_t)manager_name_data;
    manager_function[1] = (uintptr_t)manager_source_data;
    manager_function[0x2d8 / sizeof(uintptr_t)] = (uintptr_t)&manager_function;
    emitter_function[0] = (uintptr_t)emitter_name_data;
    emitter_function[1] = (uintptr_t)emitter_source_data;
    emitter_function[0x2d8 / sizeof(uintptr_t)] = (uintptr_t)&emitter_function;
    uint64_t kind[2] = {4, (uintptr_t)keys}, amount[2] = {3, 0}, flag[2] = {1, 1};
    double original = 1.25, changed = 0;
    memcpy(&amount[1], &original, sizeof original);
    const void *manager_args[4] = {kind, amount, flag, flag};
    probe_sample manager = {.function = manager_function, .args = manager_args, .count = 4};
    observe.fn(&manager);
    memcpy(&changed, &amount[1], sizeof changed);
    if (changed != original) {
        fputs("FAIL: non-Coin currency was modified\n", stderr);
        return 1;
    }
    kind[1] = (uintptr_t)coin;
    observe.fn(&manager);
    memcpy(&changed, &amount[1], sizeof changed);
    if (changed != original) {
        fputs("FAIL: manager-side Coin event was modified\n", stderr);
        return 1;
    }
    const void *emitter_args[2] = {kind, amount};
    probe_sample emitter = {.function = emitter_function, .args = emitter_args, .count = 2};
    observe.fn(&emitter);
    memcpy(&changed, &amount[1], sizeof changed);
    if (changed != 12.5) {
        fputs("FAIL: Coin emitter amount was not multiplied by ten\n", stderr);
        return 1;
    }
    memcpy(&amount[1], &original, sizeof original);
    observe.fn(&emitter);
    memcpy(&changed, &amount[1], sizeof changed);
    if (changed != original) {
        fputs("FAIL: Coin override applied more than once\n", stderr);
        return 1;
    }
    if (!stop.fn() || GetLastError() != 0x1234 || !FreeLibrary(module) || GetModuleHandleW(L"avs-native-mod.dll"))
        return 1;
    FILE *file = fopen("payload-fixture.log", "rb");
    char text[8192] = {0};
    if (!file)
        return 1;
    fread(text, 1, sizeof text - 1, file);
    fclose(file);
    if (!strstr(text, "arg_sample seq=1 ") || !strstr(text, "arg0_type=2 arg0_data=000000000000007b arg0_value=123") ||
        !strstr(text, "arg1_type=3 arg1_data=3ff8000000000000 arg1_value=1.5") || !strstr(text, "arg_sample seq=2 ") ||
        !strstr(text, "arg0_type=2 arg0_data=00000000000001c8 arg0_value=456")) {
        fputs("FAIL: UI arguments were not snapshotted on each call\n", stderr);
        return 1;
    }
    if (!strstr(text, "coin_x10 target=game_events.emit_currency_collected function_object=") ||
        !strstr(text, "original=1.25 multiplied=12.5") ||
        strstr(strstr(text, "coin_x10 target=game_events.emit_currency_collected function_object=") + 1,
               "coin_x10 target=game_events.emit_currency_collected function_object=")) {
        fputs("FAIL: one-shot Coin override audit missing or duplicated\n", stderr);
        return 1;
    }
    if (!strstr(text, "first_seen ") || !strstr(text, "interval=1 function_object=") ||
        !strstr(text, "interval=2 function_object=") || !strstr(text, "calls=3") || !strstr(text, "observed=11"))
        return 1;
    if (!strstr(text, "interval=1 function_object=") || !strstr(text, "calls_in_interval=2") ||
        !strstr(text, "interval=2 function_object=") || !strstr(text, "calls_in_interval=1"))
        return 1;
    if (!strstr(text, "name=_on_resume_button_clicked source=res://scenes/ui/pause_menu.gd") ||
        strstr(text, "name=Xon_resume_button_clicked")) {
        fputs("FAIL: first-observation names did not survive source mutation\n", stderr);
        return 1;
    }
    puts("PASS: actual payload observes, flushes and unmaps without threads or pinning");
    return 0;
}
