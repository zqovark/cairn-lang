#include "ir.h"
#include <stdlib.h>
#include <string.h>

static void set(uint64_t *bits, int r) {
    bits[(size_t)r / 64] |= UINT64_C(1) << (r % 64);
}
static bool has(const uint64_t *bits, int r) {
    return (bits[(size_t)r / 64] >> (r % 64)) & 1;
}
bool flow_build(const IRFunction *f, FlowGraph *g) {
    bool *leader = calloc(f->count + 1, sizeof(*leader));
    if (!leader)
        return false;
    leader[0] = true;
    for (size_t pc = 0; pc < f->count; pc++) {
        IRInstruction in = f->code[pc];
        if (in.op == B_JMP || in.op == B_JZ) {
            int target = in.op == B_JMP ? in.a : in.b;
            if (target < 0 || (size_t)target >= f->count) {
                free(leader);
                return false;
            }
            leader[target] = true;
        }
        if ((in.op == B_JMP || in.op == B_JZ || in.op == B_RET) && pc + 1 < f->count)
            leader[pc + 1] = true;
    }
    for (size_t pc = 0; pc < f->count; pc++)
        if (leader[pc])
            g->count++;
    if (g->count > 1024) {
        free(leader);
        g->count = 0;
        return false;
    }
    g->words = ((size_t)f->registers + 63) / 64;
    if (!g->words)
        g->words = 1;
    g->blocks = calloc(g->count, sizeof(*g->blocks));
    g->owner = calloc(f->count, sizeof(*g->owner));
    if (!g->blocks || !g->owner) {
        free(leader);
        flow_free(g);
        return false;
    }
    size_t index = 0;
    for (size_t pc = 0; pc < f->count; pc++) {
        if (leader[pc]) {
            if (pc)
                g->blocks[index++].end = pc;
            g->blocks[index].start = pc;
        }
        g->owner[pc] = (int)index;
    }
    g->blocks[index].end = f->count;
    free(leader);
    for (size_t bi = 0; bi < g->count; bi++) {
        BasicBlock *b = &g->blocks[bi];
        uint64_t *bits = calloc(4 * g->words, sizeof(*bits));
        if (!bits) {
            flow_free(g);
            return false;
        }
        b->use = bits;
        b->def = bits + g->words;
        b->in = bits + 2 * g->words;
        b->out = bits + 3 * g->words;
        IRInstruction last = f->code[b->end - 1];
        if (last.op == B_JMP || last.op == B_JZ)
            b->successors[b->successor_count++] = g->owner[last.op == B_JMP ? last.a : last.b];
        if (last.op != B_JMP && last.op != B_RET && bi + 1 < g->count)
            b->successors[b->successor_count++] = (int)bi + 1;
        for (size_t pc = b->start; pc < b->end; pc++) {
            IRInstruction in = f->code[pc];
            int args[16];
            size_t n = ir_uses(in, args);
            for (size_t j = 0; j < n; j++)
                if (!has(b->def, args[j]))
                    set(b->use, args[j]);
            int d = ir_definition(in);
            if (d >= 0)
                set(b->def, d);
        }
    }
    bool changed;
    do {
        changed = false;
        for (size_t bi = g->count; bi-- > 0;) {
            BasicBlock *b = &g->blocks[bi];
            for (size_t w = 0; w < g->words; w++) {
                uint64_t out = 0;
                for (int s = 0; s < b->successor_count; s++)
                    out |= g->blocks[b->successors[s]].in[w];
                uint64_t in = b->use[w] | (out & ~b->def[w]);
                if (in != b->in[w] || out != b->out[w])
                    changed = true;
                b->in[w] = in;
                b->out[w] = out;
            }
        }
    } while (changed);
    return true;
}
void flow_free(FlowGraph *g) {
    if (g->blocks)
        for (size_t i = 0; i < g->count; i++)
            free(g->blocks[i].use);
    free(g->blocks);
    free(g->owner);
    memset(g, 0, sizeof(*g));
}
static void print_set(FILE *out, const uint64_t *bits, int registers) {
    fputc('{', out);
    bool comma = false;
    for (int r = 0; r < registers; r++)
        if (has(bits, r)) {
            fprintf(out, "%sr%d", comma ? "," : "", r);
            comma = true;
        }
    fputc('}', out);
}
void flow_dump(const IRProgram *ir, FILE *out) {
    for (size_t fi = 0; fi < ir->count; fi++) {
        const IRFunction *f = &ir->functions[fi];
        FlowGraph g = {0};
        if (!flow_build(f, &g)) {
            fprintf(out, "function %s: CFG resource limit\n", f->name);
            continue;
        }
        fprintf(out, "function %s: %zu blocks\n", f->name, g.count);
        for (size_t i = 0; i < g.count; i++) {
            BasicBlock *b = &g.blocks[i];
            fprintf(out, "B%zu [%zu,%zu) ->", i, b->start, b->end);
            for (int s = 0; s < b->successor_count; s++)
                fprintf(out, " B%d", b->successors[s]);
            fprintf(out, " live-in=");
            print_set(out, b->in, f->registers);
            fprintf(out, " live-out=");
            print_set(out, b->out, f->registers);
            fputc('\n', out);
        }
        flow_free(&g);
    }
}
