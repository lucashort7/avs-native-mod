#include "function_name.h"
#include <windows.h>
#include <stdint.h>
#include <string.h>

#define LABEL_CODEPOINT_LIMIT 128u
#define LABEL_CHUNK_CODEPOINTS 16u

static BOOL read_exact(uintptr_t address, void *out, SIZE_T size) {
    SIZE_T received = 0;
    return address >= 0x10000 && ReadProcessMemory(GetCurrentProcess(), (const void *)address, out, size, &received) &&
           received == size;
}

static void unreadable(char *out) { strcpy(out, "<unreadable>"); }
static void truncated(char *out, size_t used, size_t capacity) {
    size_t last = used + 1 < capacity ? used : capacity - 2;
    out[last] = '~';
    out[last + 1] = '\0';
}

static void read_text(uintptr_t data, char *out, size_t capacity) {
    uintptr_t wide = 0;
    if (!data || !read_exact(data + sizeof(uintptr_t), &wide, sizeof wide) || !wide) {
        unreadable(out);
        return;
    }
    size_t used = 0;
    for (unsigned start = 0; start < LABEL_CODEPOINT_LIMIT; start += LABEL_CHUNK_CODEPOINTS) {
        uint32_t values[LABEL_CHUNK_CODEPOINTS];
        if (!read_exact(wide + start * sizeof(uint32_t), values, sizeof values)) {
            unreadable(out);
            return;
        }
        for (unsigned i = 0; i < LABEL_CHUNK_CODEPOINTS; ++i) {
            uint32_t cp = values[i];
            if (!cp) {
                out[used] = '\0';
                return;
            }
            if (cp < 0x20 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
                unreadable(out);
                return;
            }
            unsigned bytes = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
            if (used + bytes >= capacity) {
                truncated(out, used, capacity);
                return;
            }
            if (bytes == 1) {
                out[used++] = (char)cp;
            } else if (bytes == 2) {
                out[used++] = (char)(0xc0 | (cp >> 6));
                out[used++] = (char)(0x80 | (cp & 0x3f));
            } else if (bytes == 3) {
                out[used++] = (char)(0xe0 | (cp >> 12));
                out[used++] = (char)(0x80 | ((cp >> 6) & 0x3f));
                out[used++] = (char)(0x80 | (cp & 0x3f));
            } else {
                out[used++] = (char)(0xf0 | (cp >> 18));
                out[used++] = (char)(0x80 | ((cp >> 12) & 0x3f));
                out[used++] = (char)(0x80 | ((cp >> 6) & 0x3f));
                out[used++] = (char)(0x80 | (cp & 0x3f));
            }
        }
    }
    truncated(out, used, capacity);
}

void function_name_read(const void *object, function_label *label) {
    if (!label)
        return;
    unreadable(label->name);
    unreadable(label->source);
    uintptr_t pointer = (uintptr_t)object;
    uintptr_t names[2];
    uintptr_t code = 0;
    if (!read_exact(pointer, names, sizeof names) || !read_exact(pointer + 0x2d8, &code, sizeof code) || code < 0x10000)
        return;
    read_text(names[0], label->name, sizeof label->name);
    read_text(names[1], label->source, sizeof label->source);
}
