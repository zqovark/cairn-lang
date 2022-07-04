#ifndef CAIRN_IR_H
#define CAIRN_IR_H
#include "cairn.h"

enum { I_COPY = B_NOP + 1 };
typedef struct {
    int op, dst, a, b, c, args[16];
    int64_t value;
    int line;
} IRInstruction;
typedef struct {
    char *name;
    int arity, locals, registers;
    IRInstruction *code;
    size_t count, capacity;
} IRFunction;
typedef struct {
    IRFunction *functions;
    size_t count;
    int entry;
} IRProgram;
typedef struct {
    size_t start, end;
    int successors[2], successor_count;
    uint64_t *use, *def, *in, *out;
} BasicBlock;
typedef struct {
    BasicBlock *blocks;
    size_t count, words;
    int *owner;
} FlowGraph;
bool ir_build(Node *, IRProgram *, const char *);
void ir_free(IRProgram *);
void ir_dump(const IRProgram *, FILE *);
bool ir_lower(const IRProgram *, Bytecode *);
void ir_optimize(IRProgram *);
int ir_definition(IRInstruction);
size_t ir_uses(IRInstruction, int[16]);
bool flow_build(const IRFunction *, FlowGraph *);
void flow_free(FlowGraph *);
void flow_dump(const IRProgram *, FILE *);
#endif
