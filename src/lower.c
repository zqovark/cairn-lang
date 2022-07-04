#include "ir.h"
#include <stdlib.h>

static void emit(CodeFunction *f, Op op, int a, int b, int64_t value, int line) {
    if (f->count == f->capacity) {
        f->capacity = f->capacity ? f->capacity * 2 : 64;
        Instruction *v = realloc(f->code, f->capacity * sizeof(*v));
        if (!v)
            exit(2);
        f->code = v;
    }
    f->code[f->count++] = (Instruction){op, a, b, value, line};
}
static void load(CodeFunction *f, const IRFunction *ir, int reg, int line) {
    emit(f, B_LOAD, ir->locals + reg, 0, 0, line);
}
static void store(CodeFunction *f, const IRFunction *ir, int reg, int line) {
    emit(f, B_STORE, ir->locals + reg, 0, 0, line);
}
bool ir_lower(const IRProgram *ir, Bytecode *out) {
    out->count = ir->count;
    out->entry = ir->entry;
    out->functions = calloc(out->count, sizeof(*out->functions));
    if (!out->functions)
        return false;
    for (size_t i = 0; i < ir->count; i++) {
        const IRFunction *f = &ir->functions[i];
        CodeFunction *b = &out->functions[i];
        b->name = f->name;
        b->arity = f->arity;
        b->slots = f->locals + f->registers;
        size_t *mapping = calloc(f->count + 1, sizeof(*mapping));
        if (!mapping)
            return false;
        for (size_t pc = 0; pc < f->count; pc++) {
            IRInstruction in = f->code[pc];
            mapping[pc] = b->count;
            int line = in.line;
            if (in.op >= B_ADD && in.op <= B_GE) {
                load(b, f, in.a, line);
                load(b, f, in.b, line);
                emit(b, (Op)in.op, 0, 0, 0, line);
                store(b, f, in.dst, line);
                continue;
            }
            switch (in.op) {
            case B_CONST:
                emit(b, B_CONST, 0, 0, in.value, line);
                store(b, f, in.dst, line);
                break;
            case B_LOAD:
                emit(b, B_LOAD, in.a, 0, 0, line);
                store(b, f, in.dst, line);
                break;
            case B_STORE:
                load(b, f, in.a, line);
                emit(b, B_STORE, in.dst, 0, 0, line);
                break;
            case I_COPY:
                load(b, f, in.a, line);
                store(b, f, in.dst, line);
                break;
            case B_NOT:
            case B_NEG:
                load(b, f, in.a, line);
                emit(b, (Op)in.op, 0, 0, 0, line);
                store(b, f, in.dst, line);
                break;
            case B_JMP:
                emit(b, B_JMP, in.a, 0, 0, line);
                break;
            case B_JZ:
                load(b, f, in.a, line);
                emit(b, B_JZ, in.b, 0, 0, line);
                break;
            case B_CALL:
                for (int j = 0; j < in.b; j++)
                    load(b, f, in.args[j], line);
                emit(b, B_CALL, in.a, in.b, 0, line);
                store(b, f, in.dst, line);
                break;
            case B_RET:
                load(b, f, in.a, line);
                emit(b, B_RET, 0, 0, 0, line);
                break;
            case B_PRINT:
                load(b, f, in.a, line);
                emit(b, B_PRINT, in.b, 0, 0, line);
                break;
            case B_ALLOC:
                emit(b, B_ALLOC, in.dst, in.b, 0, line);
                break;
            case B_AGET:
                load(b, f, in.a, line);
                load(b, f, in.b, line);
                emit(b, B_AGET, 0, 0, 0, line);
                store(b, f, in.dst, line);
                break;
            case B_ASET:
                load(b, f, in.a, line);
                load(b, f, in.b, line);
                load(b, f, in.c, line);
                emit(b, B_ASET, 0, 0, 0, line);
                break;
            default:
                break;
            }
        }
        mapping[f->count] = b->count;
        for (size_t pc = 0; pc < b->count; pc++)
            if (b->code[pc].op == B_JMP || b->code[pc].op == B_JZ) {
                int target = b->code[pc].a;
                if (target < 0 || (size_t)target > f->count) {
                    free(mapping);
                    return false;
                }
                b->code[pc].a = (int)mapping[target];
            }
        free(mapping);
    }
    return true;
}
