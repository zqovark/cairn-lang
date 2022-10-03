#include "ir.h"
#include <limits.h>
#include <stdlib.h>

static bool removable(IRInstruction in) {
    switch (in.op) {
    case B_CONST: case B_LOAD: case I_COPY: case B_NOT:
    case B_EQ: case B_NE: case B_LT: case B_LE: case B_GT: case B_GE:
        return true;
    default:
        return false;
    }
}
static void optimize_function(IRFunction *f) {
    size_t registers = (size_t)f->registers;
    int *defs = calloc(registers + 1, sizeof(*defs));
    bool *known = calloc(registers + 1, sizeof(*known));
    int64_t *values = calloc(registers + 1, sizeof(*values));
    if (!defs || !known || !values)
        exit(2);
    for (size_t pc = 0; pc < f->count; pc++) {
        int d = ir_definition(f->code[pc]);
        if (d >= 0)
            defs[d]++;
    }
    for (size_t pc = 0; pc < f->count; pc++) {
        IRInstruction *in = &f->code[pc];
        int d = ir_definition(*in);
        int64_t result = 0;
        bool fold = false;
        if (in->op == B_CONST) {
            result = in->value;
            fold = true;
        } else if (in->op == I_COPY && known[in->a]) {
            result = values[in->a];
            fold = true;
        } else if (in->op >= B_ADD && in->op <= B_GE && known[in->a] && known[in->b])
            fold = checked_binary((Op)in->op, values[in->a], values[in->b], &result);
        else if (in->op == B_NOT && known[in->a]) {
            result = !values[in->a];
            fold = true;
        } else if (in->op == B_NEG && known[in->a] && values[in->a] != INT64_MIN) {
            result = -values[in->a];
            fold = true;
        }
        if (fold) {
            in->op = B_CONST;
            in->value = result;
            if (d >= 0 && defs[d] == 1) {
                known[d] = true;
                values[d] = result;
            }
        }
        if (in->op == B_JZ && known[in->a]) {
            if (!values[in->a]) {
                in->op = B_JMP;
                in->a = in->b;
            } else
                in->op = B_NOP;
        }
    }
    FlowGraph graph = {0};
    if (flow_build(f, &graph)) {
        bool *reachable = calloc(graph.count, sizeof(*reachable));
        size_t *work = calloc(graph.count, sizeof(*work));
        if (!reachable || !work)
            exit(2);
        size_t head = 0, tail = 0;
        reachable[0] = true;
        work[tail++] = 0;
        while (head < tail) {
            BasicBlock *block = &graph.blocks[work[head++]];
            for (int i = 0; i < block->successor_count; i++) {
                int target = block->successors[i];
                if (!reachable[target]) {
                    reachable[target] = true;
                    work[tail++] = (size_t)target;
                }
            }
        }
        for (size_t bi = 0; bi < graph.count; bi++)
            if (!reachable[bi])
                for (size_t pc = graph.blocks[bi].start; pc < graph.blocks[bi].end; pc++)
                    f->code[pc].op = B_NOP;
        free(reachable);
        free(work);
        flow_free(&graph);
    }
    bool changed;
    do {
        changed = false;
        for (size_t r = 0; r < registers; r++)
            defs[r] = 0;
        for (size_t pc = 0; pc < f->count; pc++) {
            int args[16];
            size_t n = ir_uses(f->code[pc], args);
            for (size_t i = 0; i < n; i++)
                defs[args[i]]++;
        }
        for (size_t pc = 0; pc < f->count; pc++) {
            IRInstruction *in = &f->code[pc];
            int d = ir_definition(*in);
            if (d >= 0 && !defs[d] && removable(*in)) {
                in->op = B_NOP;
                changed = true;
            }
        }
    } while (changed);
    free(defs);
    free(known);
    free(values);
}
void ir_optimize(IRProgram *ir) {
    for (size_t i = 0; i < ir->count; i++)
        optimize_function(&ir->functions[i]);
}
