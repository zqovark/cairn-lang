#include "cairn.h"
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool checked_binary(Op op, int64_t a, int64_t b, int64_t *out) {
    switch (op) {
    case B_ADD:
        if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
            return false;
        *out = a + b;
        break;
    case B_SUB:
        if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
            return false;
        *out = a - b;
        break;
    case B_MUL:
        if (a > 0) {
            if ((b > 0 && a > INT64_MAX / b) || (b < 0 && b < INT64_MIN / a))
                return false;
        } else if (a < 0) {
            if ((b > 0 && a < INT64_MIN / b) || (b < 0 && a < INT64_MAX / b))
                return false;
        }
        *out = a * b;
        break;
    case B_DIV:
    case B_MOD:
        if (b == 0 || (a == INT64_MIN && b == -1))
            return false;
        *out = op == B_DIV ? a / b : a % b;
        break;
    case B_EQ:
        *out = a == b;
        break;
    case B_NE:
        *out = a != b;
        break;
    case B_LT:
        *out = a < b;
        break;
    case B_LE:
        *out = a <= b;
        break;
    case B_GT:
        *out = a > b;
        break;
    case B_GE:
        *out = a >= b;
        break;
    default:
        return false;
    }
    return true;
}
typedef struct {
    int64_t *data;
    size_t size;
    uint64_t owner;
} Array;
typedef struct {
    const Bytecode *program;
    FILE *out, *errors;
    uint64_t budget, serial;
    Array arrays[4096];
    size_t bytes;
} VM;
static bool execute(VM *vm, int fn, const int64_t *args, int argc, int depth, int64_t *result) {
    if (depth >= 256) {
        fputs("runtime error: call depth limit exceeded\n", vm->errors);
        return false;
    }
    const CodeFunction *f = &vm->program->functions[fn];
    int64_t *locals = calloc((size_t)f->slots + 1, sizeof(*locals)),
            *stack = calloc(8192, sizeof(*stack));
    if (!locals || !stack) {
        free(locals);
        free(stack);
        fputs("runtime error: allocation failed\n", vm->errors);
        return false;
    }
    for (int i = 0; i < argc; i++)
        locals[i] = args[i];
    size_t pc = 0, sp = 0;
    bool ok = false;
    uint64_t owner = ++vm->serial;
    const char *reason = "invalid instruction";
    int line = 1;
    while (pc < f->count) {
        Instruction in = f->code[pc++];
        line = in.line;
        if (!vm->budget) {
            reason = "instruction budget exceeded";
            goto fail;
        }
        vm->budget--;
        if (in.op == B_CONST) {
            stack[sp++] = in.value;
            continue;
        }
        if (in.op == B_LOAD) {
            stack[sp++] = locals[in.a];
            continue;
        }
        if (in.op == B_STORE) {
            locals[in.a] = stack[--sp];
            continue;
        }
        if (in.op >= B_ADD && in.op <= B_GE) {
            int64_t b = stack[--sp], a = stack[--sp], v;
            if (!checked_binary(in.op, a, b, &v)) {
                reason = ((in.op == B_DIV || in.op == B_MOD) && b == 0) ? "division by zero"
                                                                        : "integer overflow";
                goto fail;
            }
            stack[sp++] = v;
            continue;
        }
        switch (in.op) {
        case B_NOT:
            stack[sp - 1] = !stack[sp - 1];
            break;
        case B_NEG:
            if (stack[sp - 1] == INT64_MIN) {
                reason = "integer overflow";
                goto fail;
            }
            stack[sp - 1] = -stack[sp - 1];
            break;
        case B_JMP:
            pc = (size_t)in.a;
            break;
        case B_JZ:
            if (!stack[--sp])
                pc = (size_t)in.a;
            break;
        case B_CALL: {
            int64_t v;
            sp -= (size_t)in.b;
            if (!execute(vm, in.a, stack + sp, in.b, depth + 1, &v))
                goto done;
            stack[sp++] = v;
            break;
        }
        case B_RET:
            *result = stack[--sp];
            ok = true;
            goto done;
        case B_PRINT: {
            int64_t v = stack[--sp];
            int written = in.a ? fprintf(vm->out, "%s\n", v ? "true" : "false")
                               : fprintf(vm->out, "%" PRId64 "\n", v);
            if (written < 0) {
                reason = "output write failed";
                goto fail;
            }
            break;
        }
        case B_POP:
            sp--;
            break;
        case B_NOP:
            break;
        case B_ALLOC: {
            int handle = (int)locals[in.a];
            Array *array = NULL;
            if (handle > 0 && handle <= 4096 && vm->arrays[handle - 1].data &&
                vm->arrays[handle - 1].owner == owner)
                array = &vm->arrays[handle - 1];
            else {
                for (int i = 0; i < 4096; i++)
                    if (!vm->arrays[i].data) {
                        handle = i + 1;
                        array = &vm->arrays[i];
                        break;
                    }
            }
            if (!array) {
                reason = "array handle limit exceeded";
                goto fail;
            }
            if (!array->data) {
                size_t bytes = (size_t)in.b * sizeof(int64_t);
                if (vm->bytes + bytes > 64 * 1024 * 1024) {
                    reason = "array memory limit exceeded";
                    goto fail;
                }
                array->data = calloc((size_t)in.b, sizeof(int64_t));
                if (!array->data) {
                    reason = "allocation failed";
                    goto fail;
                }
                array->size = (size_t)in.b;
                array->owner = owner;
                vm->bytes += bytes;
            } else {
                memset(array->data, 0, array->size * sizeof(int64_t));
            }
            locals[in.a] = handle;
            break;
        }
        case B_AGET:
        case B_ASET: {
            int64_t value = 0;
            if (in.op == B_ASET)
                value = stack[--sp];
            int64_t index = stack[--sp], handle = stack[--sp];
            if (handle < 1 || handle > 4096 || !vm->arrays[handle - 1].data) {
                reason = "invalid array handle";
                goto fail;
            }
            Array *a = &vm->arrays[handle - 1];
            if (index < 0 || (uint64_t)index >= a->size) {
                reason = "array index out of bounds";
                goto fail;
            }
            if (in.op == B_AGET)
                stack[sp++] = a->data[index];
            else
                a->data[index] = value;
            break;
        }
        default:
            goto fail;
        }
    }
    reason = "function fell through";
fail:
    fprintf(vm->errors, "runtime error at %s:%d: %s\n", f->name, line, reason);
done:
    for (int i = 0; i < 4096; i++)
        if (vm->arrays[i].data && vm->arrays[i].owner == owner) {
            vm->bytes -= vm->arrays[i].size * sizeof(int64_t);
            free(vm->arrays[i].data);
            vm->arrays[i] = (Array){0};
        }
    free(locals);
    free(stack);
    return ok;
}
bool vm_run(const Bytecode *b, FILE *out, FILE *errors, uint64_t budget, int64_t *result) {
    if (!bytecode_verify(b, errors))
        return false;
    VM *vm = calloc(1, sizeof(*vm));
    if (!vm)
        return false;
    vm->program = b;
    vm->out = out;
    vm->errors = errors;
    vm->budget = budget;
    bool ok = execute(vm, b->entry, NULL, 0, 0, result);
    free(vm);
    return ok;
}
