#include "cairn.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const char *names[] = {"const", "load", "store", "add",   "sub",  "mul",  "div", "mod", "eq",
                              "ne",    "lt",   "le",    "gt",    "ge",   "not",  "neg", "jmp", "jz",
                              "call",  "ret",  "print", "alloc", "aget", "aset", "pop", "nop"};
const char *op_name(Op op) {
    return names[op];
}
void bytecode_free(Bytecode *b) {
    for (size_t i = 0; i < b->count; i++) {
        free(b->functions[i].code);
        if (b->owns_names)
            free(b->functions[i].name);
    }
    free(b->functions);
    memset(b, 0, sizeof(*b));
}
void bytecode_dump(const Bytecode *b, FILE *out) {
    for (size_t i = 0; i < b->count; i++) {
        const CodeFunction *f = &b->functions[i];
        fprintf(out, "function %s/%d slots=%d\n", f->name, f->arity, f->slots);
        for (size_t pc = 0; pc < f->count; pc++) {
            Instruction in = f->code[pc];
            fprintf(out, "%04zu %-7s %d %d %" PRId64 " ; line %d\n", pc, op_name(in.op), in.a, in.b,
                    in.value, in.line);
        }
    }
}
bool bytecode_verify(const Bytecode *b, FILE *errors) {
    if (!b->count || b->count > 256 || b->entry < 0 || (size_t)b->entry >= b->count ||
        b->functions[b->entry].arity != 0) {
        fputs("invalid bytecode entry\n", errors);
        return false;
    }
    for (size_t fi = 0; fi < b->count; fi++) {
        const CodeFunction *f = &b->functions[fi];
        if (f->slots < 0 || f->slots > 16384 || f->arity < 0 || f->arity > 16 ||
            f->arity > f->slots || !f->count || f->count > 100000) {
            fputs("invalid bytecode function bounds\n", errors);
            return false;
        }
        for (size_t pc = 0; pc < f->count; pc++) {
            Instruction in = f->code[pc];
            if (in.op < 0 || in.op > B_NOP)
                goto invalid;
            if ((in.op == B_LOAD || in.op == B_STORE || in.op == B_ALLOC) &&
                (in.a < 0 || in.a >= f->slots))
                goto invalid;
            if (in.op == B_ALLOC && (in.b < 1 || in.b > 65536))
                goto invalid;
            if ((in.op == B_JMP || in.op == B_JZ) && (in.a < 0 || (size_t)in.a >= f->count))
                goto invalid;
            if (in.op == B_PRINT && (in.a < 0 || in.a > 1))
                goto invalid;
            if (in.op == B_CALL &&
                (in.a < 0 || (size_t)in.a >= b->count || in.b != b->functions[in.a].arity))
                goto invalid;
        }
        int *depth = malloc(f->count * sizeof(*depth));
        size_t *work = malloc(f->count * sizeof(*work));
        if (!depth || !work) {
            free(depth);
            free(work);
            return false;
        }
        for (size_t i = 0; i < f->count; i++)
            depth[i] = -1;
        size_t head = 0, tail = 0;
        depth[0] = 0;
        work[tail++] = 0;
        bool ok = true;
        while (head < tail && ok) {
            size_t pc = work[head++];
            Instruction in = f->code[pc];
            int need = 0, delta = 0;
            if (in.op == B_CONST || in.op == B_LOAD)
                delta = 1;
            else if (in.op >= B_ADD && in.op <= B_GE) {
                need = 2;
                delta = -1;
            } else if (in.op == B_AGET) {
                need = 2;
                delta = -1;
            } else if (in.op == B_ASET) {
                need = 3;
                delta = -3;
            } else if (in.op == B_NOT || in.op == B_NEG)
                need = 1;
            else if (in.op == B_CALL) {
                need = in.b;
                delta = 1 - in.b;
            } else if (in.op == B_STORE || in.op == B_JZ || in.op == B_PRINT || in.op == B_POP ||
                       in.op == B_RET) {
                need = 1;
                delta = -1;
            }
            int after = depth[pc] + delta;
            if (depth[pc] < need || after > 8192) {
                ok = false;
                break;
            }
            if (in.op == B_RET) {
                if (after != 0)
                    ok = false;
                continue;
            }
            size_t targets[2], count = 0;
            if (in.op == B_JMP || in.op == B_JZ)
                targets[count++] = (size_t)in.a;
            if (in.op != B_JMP) {
                if (pc + 1 >= f->count) {
                    ok = false;
                    break;
                }
                targets[count++] = pc + 1;
            }
            for (size_t i = 0; i < count; i++) {
                size_t t = targets[i];
                if (depth[t] < 0) {
                    depth[t] = after;
                    work[tail++] = t;
                } else if (depth[t] != after) {
                    ok = false;
                    break;
                }
            }
        }
        free(depth);
        free(work);
        if (!ok)
            goto invalid;
        continue;
    invalid:
        fprintf(errors, "invalid bytecode in function %s\n", f->name ? f->name : "?");
        return false;
    }
    return true;
}
bool bytecode_write(const Bytecode *b, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return false;
    }
    fprintf(f, "CAIRNBC1\n%zu %d\n", b->count, b->entry);
    for (size_t i = 0; i < b->count; i++) {
        CodeFunction *cf = &b->functions[i];
        fprintf(f, "%s %d %d %zu\n", cf->name, cf->arity, cf->slots, cf->count);
        for (size_t j = 0; j < cf->count; j++) {
            Instruction in = cf->code[j];
            fprintf(f, "%d %d %d %" PRId64 " %d\n", (int)in.op, in.a, in.b, in.value, in.line);
        }
    }
    bool ok = !ferror(f);
    if (fclose(f) != 0)
        ok = false;
    return ok;
}
static bool word(FILE *f, char *out, size_t capacity) {
    int ch;
    do {
        ch = fgetc(f);
    } while (ch != EOF && isspace((unsigned char)ch));
    if (ch == EOF)
        return false;
    size_t length = 0;
    bool long_word = false;
    do {
        if (length + 1 < capacity)
            out[length++] = (char)ch;
        else
            long_word = true;
        ch = fgetc(f);
    } while (ch != EOF && !isspace((unsigned char)ch));
    out[length] = 0;
    return !long_word;
}
static bool number(FILE *f, int64_t low, int64_t high, int64_t *out) {
    char text[128], *end;
    if (!word(f, text, sizeof(text)))
        return false;
    errno = 0;
    intmax_t value = strtoimax(text, &end, 10);
    if (errno || *end || end == text || value < low || value > high)
        return false;
    *out = (int64_t)value;
    return true;
}
bool bytecode_read(Bytecode *b, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return false;
    }
    bool ok = false;
    char magic[32];
    int64_t count, entry;
    b->owns_names = true;
    if (fseek(f, 0, SEEK_END) != 0)
        goto done;
    long bytes = ftell(f);
    if (bytes < 0 || bytes > 16 * 1024 * 1024)
        goto done;
    rewind(f);
    if (!fgets(magic, sizeof(magic), f) || strcmp(magic, "CAIRNBC1\n") ||
        !number(f, 1, 256, &count) || !number(f, 0, count - 1, &entry))
        goto done;
    b->count = (size_t)count;
    b->entry = (int)entry;
    b->functions = calloc(b->count, sizeof(*b->functions));
    if (!b->functions) {
        b->count = 0;
        goto done;
    }
    for (size_t i = 0; i < b->count; i++) {
        CodeFunction *cf = &b->functions[i];
        char name[64];
        int64_t arity, slots, instructions;
        if (!word(f, name, sizeof(name)) || !(isalpha((unsigned char)name[0]) || name[0] == '_'))
            goto done;
        for (size_t j = 1; name[j]; j++)
            if (!(isalnum((unsigned char)name[j]) || name[j] == '_'))
                goto done;
        if (!number(f, 0, 16, &arity) || !number(f, arity, 16384, &slots) ||
            !number(f, 1, 100000, &instructions))
            goto done;
        cf->arity = (int)arity;
        cf->slots = (int)slots;
        cf->count = (size_t)instructions;
        cf->name = malloc(strlen(name) + 1);
        cf->code = calloc(cf->count, sizeof(*cf->code));
        if (!cf->name || !cf->code)
            goto done;
        strcpy(cf->name, name);
        for (size_t j = 0; j < cf->count; j++) {
            int64_t op, a, rhs, value, line;
            if (!number(f, 0, B_NOP, &op) || !number(f, INT_MIN, INT_MAX, &a) ||
                !number(f, INT_MIN, INT_MAX, &rhs) || !number(f, INT64_MIN, INT64_MAX, &value) ||
                !number(f, 1, INT_MAX, &line))
                goto done;
            cf->code[j] = (Instruction){(Op)op, (int)a, (int)rhs, value, (int)line};
        }
    }
    {
        int ch;
        while ((ch = fgetc(f)) != EOF)
            if (!isspace((unsigned char)ch))
                goto done;
    }
    ok = bytecode_verify(b, stderr);
done:
    fclose(f);
    if (!ok) {
        fputs("cairn: malformed bytecode\n", stderr);
        bytecode_free(b);
    }
    return ok;
}
