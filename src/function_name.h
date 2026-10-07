#ifndef AVS_FUNCTION_NAME_H
#define AVS_FUNCTION_NAME_H

/* Snapshot the observed AVS function object's labels before the object dies.
 * Failed reads leave explicit markers, never pointers to mutable game memory. */
typedef struct function_label {
    char name[96];
    char source[192];
} function_label;

void function_name_read(const void *object, function_label *label);

#endif
