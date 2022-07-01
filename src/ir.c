#include "ir.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef struct Loop {
    int start;
    size_t *breaks, count;
    struct Loop *parent;
} Loop;
typedef struct {
    IRFunction *f;
    Loop *loop;
} Builder;
static int temporary(Builder *b) {
    if (b->f->registers >= 8192) {
        fputs("cairn: IR register limit exceeded\n", stderr);
        exit(2);
    }
    return b->f->registers++;
}
static size_t emit(Builder *b, int op, int dst, int a, int rhs, int c, int64_t value, int line) {
    IRFunction *f = b->f;
    if (f->count >= 50000) {
        fputs("cairn: IR instruction limit exceeded\n", stderr);
        exit(2);
    }
    if (f->count == f->capacity) {
        f->capacity = f->capacity ? f->capacity * 2 : 64;
        IRInstruction *v = realloc(f->code, f->capacity * sizeof(*v));
        if (!v)
            exit(2);
        f->code = v;
    }
    size_t pc = f->count++;
    f->code[pc] = (IRInstruction){
        .op = op, .dst = dst, .a = a, .b = rhs, .c = c, .value = value, .line = line};
    return pc;
}
static int literal(Builder *b, int64_t value, int line) {
    int r = temporary(b);
    emit(b, B_CONST, r, 0, 0, 0, value, line);
    return r;
}
static int operation(TokenKind k) {
    switch (k) {
    case T_PLUS:
        return B_ADD;
    case T_MINUS:
        return B_SUB;
    case T_STAR:
        return B_MUL;
    case T_SLASH:
        return B_DIV;
    case T_PERCENT:
        return B_MOD;
    case T_EQEQ:
        return B_EQ;
    case T_NE:
        return B_NE;
    case T_LT:
        return B_LT;
    case T_LE:
        return B_LE;
    case T_GT:
        return B_GT;
    case T_GE:
        return B_GE;
    default:
        return B_NOP;
    }
}
static int expression(Builder *b, Node *n) {
    int line = n->token.line, r, a, v;
    size_t branch, end;
    switch (n->kind) {
    case N_INT:
    case N_BOOL:
        return literal(b, n->value, line);
    case N_VAR:
        r = temporary(b);
        emit(b, B_LOAD, r, n->index, 0, 0, 0, line);
        return r;
    case N_UNARY:
        a = expression(b, n->a);
        r = temporary(b);
        emit(b, n->op == T_NOT ? B_NOT : B_NEG, r, a, 0, 0, 0, line);
        return r;
    case N_INDEX:
        a = expression(b, n->a);
        v = expression(b, n->b);
        r = temporary(b);
        emit(b, B_AGET, r, a, v, 0, 0, line);
        return r;
    case N_CALL: {
        int args[16];
        for (size_t i = 0; i < n->count; i++)
            args[i] = expression(b, n->items[i]);
        r = temporary(b);
        size_t pc = emit(b, B_CALL, r, n->index, (int)n->count, 0, 0, line);
        memcpy(b->f->code[pc].args, args, n->count * sizeof(*args));
        return r;
    }
    case N_BINARY:
        a = expression(b, n->a);
        if (n->op == T_AND || n->op == T_OR) {
            r = temporary(b);
            branch = emit(b, B_JZ, 0, a, 0, 0, 0, line);
            if (n->op == T_OR) {
                emit(b, B_CONST, r, 0, 0, 0, 1, line);
                end = emit(b, B_JMP, 0, 0, 0, 0, 0, line);
                b->f->code[branch].b = (int)b->f->count;
                v = expression(b, n->b);
                emit(b, I_COPY, r, v, 0, 0, 0, line);
            } else {
                v = expression(b, n->b);
                emit(b, I_COPY, r, v, 0, 0, 0, line);
                end = emit(b, B_JMP, 0, 0, 0, 0, 0, line);
                b->f->code[branch].b = (int)b->f->count;
                emit(b, B_CONST, r, 0, 0, 0, 0, line);
            }
            b->f->code[end].a = (int)b->f->count;
            return r;
        }
        v = expression(b, n->b);
        r = temporary(b);
        emit(b, operation(n->op), r, a, v, 0, 0, line);
        return r;
    default:
        return literal(b, 0, line);
    }
}
static void statement(Builder *b, Node *n) {
    int line = n->token.line, a, value;
    size_t branch, end;
    switch (n->kind) {
    case N_BLOCK:
        for (size_t i = 0; i < n->count; i++)
            statement(b, n->items[i]);
        break;
    case N_LET:
        if (n->type.length)
            emit(b, B_ALLOC, n->index, 0, n->type.length, 0, 0, line);
        else {
            value = n->a ? expression(b, n->a) : literal(b, 0, line);
            emit(b, B_STORE, n->index, value, 0, 0, 0, line);
        }
        break;
    case N_ASSIGN:
        if (n->a->kind == N_INDEX) {
            a = expression(b, n->a->a);
            int index = expression(b, n->a->b);
            value = expression(b, n->b);
            emit(b, B_ASET, 0, a, index, value, 0, line);
        } else {
            value = expression(b, n->b);
            emit(b, B_STORE, n->a->index, value, 0, 0, 0, line);
        }
        break;
    case N_EXPR:
        (void)expression(b, n->a);
        break;
    case N_PRINT:
        a = expression(b, n->a);
        emit(b, B_PRINT, 0, a, n->a->type.kind == TY_BOOL, 0, 0, line);
        break;
    case N_RETURN:
        a = n->a ? expression(b, n->a) : literal(b, 0, line);
        emit(b, B_RET, 0, a, 0, 0, 0, line);
        break;
    case N_IF:
        a = expression(b, n->a);
        branch = emit(b, B_JZ, 0, a, 0, 0, 0, line);
        statement(b, n->b);
        end = emit(b, B_JMP, 0, 0, 0, 0, 0, line);
        b->f->code[branch].b = (int)b->f->count;
        if (n->c)
            statement(b, n->c);
        b->f->code[end].a = (int)b->f->count;
        break;
    case N_WHILE: {
        Loop loop = {(int)b->f->count, NULL, 0, b->loop};
        b->loop = &loop;
        a = expression(b, n->a);
        end = emit(b, B_JZ, 0, a, 0, 0, 0, line);
        statement(b, n->b);
        emit(b, B_JMP, 0, loop.start, 0, 0, 0, line);
        b->f->code[end].b = (int)b->f->count;
        for (size_t i = 0; i < loop.count; i++) {
            b->f->code[loop.breaks[i]].a = (int)b->f->count;
        }
        free(loop.breaks);
        b->loop = loop.parent;
        break;
    }
    case N_CONTINUE:
        emit(b, B_JMP, 0, b->loop->start, 0, 0, 0, line);
        break;
    case N_BREAK: {
        size_t pc = emit(b, B_JMP, 0, 0, 0, 0, 0, line);
        size_t *v = realloc(b->loop->breaks, (b->loop->count + 1) * sizeof(*v));
        if (!v)
            exit(2);
        b->loop->breaks = v;
        v[b->loop->count++] = pc;
        break;
    }
    default:
        break;
    }
}
bool ir_build(Node *root, IRProgram *ir, const char *path) {
    (void)path;
    ir->count = root->count;
    ir->functions = calloc(ir->count, sizeof(*ir->functions));
    if (!ir->functions)
        return false;
    for (size_t i = 0; i < ir->count; i++) {
        Node *f = root->items[i];
        IRFunction *out = &ir->functions[i];
        out->name = f->name;
        out->arity = (int)f->count;
        out->locals = f->aux;
        Builder b = {out, NULL};
        statement(&b, f->a);
        int zero = literal(&b, 0, f->token.line);
        emit(&b, B_RET, 0, zero, 0, 0, 0, f->token.line);
        if (!strcmp(f->name, "main"))
            ir->entry = (int)i;
    }
    return true;
}
void ir_free(IRProgram *ir) {
    for (size_t i = 0; i < ir->count; i++)
        free(ir->functions[i].code);
    free(ir->functions);
    memset(ir, 0, sizeof(*ir));
}
int ir_definition(IRInstruction in) {
    if (in.op == B_CONST || in.op == B_LOAD || in.op == B_CALL || in.op == B_AGET ||
        in.op == I_COPY || (in.op >= B_ADD && in.op <= B_NEG))
        return in.dst;
    return -1;
}
size_t ir_uses(IRInstruction in, int args[16]) {
    if (in.op == B_CALL) {
        memcpy(args, in.args, (size_t)in.b * sizeof(*args));
        return (size_t)in.b;
    }
    if ((in.op >= B_ADD && in.op <= B_GE) || in.op == B_AGET) {
        args[0] = in.a;
        args[1] = in.b;
        return 2;
    }
    if (in.op == B_ASET) {
        args[0] = in.a;
        args[1] = in.b;
        args[2] = in.c;
        return 3;
    }
    if (in.op == B_STORE || in.op == B_NOT || in.op == B_NEG || in.op == B_JZ || in.op == B_RET ||
        in.op == B_PRINT || in.op == I_COPY) {
        args[0] = in.a;
        return 1;
    }
    return 0;
}
void ir_dump(const IRProgram *ir, FILE *out) {
    for (size_t i = 0; i < ir->count; i++) {
        const IRFunction *f = &ir->functions[i];
        fprintf(out, "function %s/%d locals=%d temporaries=%d\n", f->name, f->arity, f->locals,
                f->registers);
        for (size_t pc = 0; pc < f->count; pc++) {
            IRInstruction in = f->code[pc];
            if (in.op == B_NOP)
                continue;
            fprintf(out, "%04zu ", pc);
            int dst = ir_definition(in);
            if (dst >= 0)
                fprintf(out, "r%d = ", dst);
            fprintf(out, "%s", in.op == I_COPY ? "copy" : op_name((Op)in.op));
            if (in.op == B_CONST)
                fprintf(out, " %" PRId64, in.value);
            else if (in.op == B_LOAD || in.op == B_ALLOC)
                fprintf(out, " local%d%s", in.op == B_LOAD ? in.a : in.dst,
                        in.op == B_ALLOC ? " (array)" : "");
            else if (in.op == B_STORE)
                fprintf(out, " local%d, r%d", in.dst, in.a);
            else if (in.op == B_JMP)
                fprintf(out, " L%d", in.a);
            else if (in.op == B_JZ)
                fprintf(out, " r%d, L%d", in.a, in.b);
            else {
                int uses[16];
                size_t count = ir_uses(in, uses);
                if (in.op == B_CALL)
                    fprintf(out, " function%d", in.a);
                for (size_t j = 0; j < count; j++)
                    fprintf(out, "%s r%d", j ? "," : "", uses[j]);
            }
            fprintf(out, " ; line %d\n", in.line);
        }
    }
}
