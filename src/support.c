#include "cairn.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void *arena_alloc(Arena *a, size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) {
        fputs("cairn: out of memory\n", stderr);
        exit(2);
    }
    if (a->count == a->capacity) {
        a->capacity = a->capacity ? a->capacity * 2 : 64;
        void **v = realloc(a->items, a->capacity * sizeof(*v));
        if (!v) {
            free(p);
            fputs("cairn: out of memory\n", stderr);
            exit(2);
        }
        a->items = v;
    }
    a->items[a->count++] = p;
    return p;
}
char *arena_string(Arena *a, const char *s, size_t n) {
    char *p = arena_alloc(a, n + 1);
    memcpy(p, s, n);
    return p;
}
void arena_free(Arena *a) {
    for (size_t i = 0; i < a->count; i++)
        free(a->items[i]);
    free(a->items);
    memset(a, 0, sizeof(*a));
}
void diagnostic(const char *path, Token t, const char *format, ...) {
    fprintf(stderr, "%s:%d:%d: error: ", path ? path : "<source>", t.line, t.column);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}
const char *type_name(Type t) {
    if (t.length)
        return t.kind == TY_BOOL ? "bool[]" : "int[]";
    return t.kind == TY_INT ? "int" : t.kind == TY_BOOL ? "bool" : "void";
}
