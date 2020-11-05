#include "cairn.h"
#include <stdlib.h>
#include <string.h>

typedef struct Loop {
    size_t start;
    size_t *breaks;
    size_t count;
    struct Loop *parent;
} Loop;
typedef struct {
    CodeFunction *f;
    Loop *loop;
} Emitter;
static size_t emit(Emitter *e, Op op, int a, int b, int64_t value, int line) {
    CodeFunction *f = e->f;
    if (f->count == f->capacity) {
        f->capacity = f->capacity ? f->capacity * 2 : 64;
        Instruction *v = realloc(f->code, f->capacity * sizeof(*v));
        if (!v) {
            fputs("cairn: out of memory\n", stderr);
            exit(2);
        }
        f->code = v;
    }
    size_t at = f->count++;
    f->code[at] = (Instruction){op, a, b, value, line};
    return at;
}
static Op binary(TokenKind k) {
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
static void expression(Emitter *e, Node *n) {
    int line = n->token.line;
    switch (n->kind) {
    case N_INT:
    case N_BOOL:
        emit(e, B_CONST, 0, 0, n->value, line);
        break;
    case N_VAR:
        emit(e, B_LOAD, n->index, 0, 0, line);
        break;
    case N_INDEX:
        expression(e, n->a);
        expression(e, n->b);
        emit(e, B_AGET, 0, 0, 0, line);
        break;
    case N_UNARY:
        expression(e, n->a);
        emit(e, n->op == T_NOT ? B_NOT : B_NEG, 0, 0, 0, line);
        break;
    case N_CALL:
        for (size_t i = 0; i < n->count; i++)
            expression(e, n->items[i]);
        emit(e, B_CALL, n->index, (int)n->count, 0, line);
        break;
    case N_BINARY:
        if (n->op == T_AND || n->op == T_OR) {
            expression(e, n->a);
            size_t branch = emit(e, B_JZ, 0, 0, 0, line);
            if (n->op == T_OR) {
                emit(e, B_CONST, 0, 0, 1, line);
                size_t end = emit(e, B_JMP, 0, 0, 0, line);
                e->f->code[branch].a = (int)e->f->count;
                expression(e, n->b);
                e->f->code[end].a = (int)e->f->count;
            } else {
                expression(e, n->b);
                size_t end = emit(e, B_JMP, 0, 0, 0, line);
                e->f->code[branch].a = (int)e->f->count;
                emit(e, B_CONST, 0, 0, 0, line);
                e->f->code[end].a = (int)e->f->count;
            }
        } else {
            expression(e, n->a);
            expression(e, n->b);
            emit(e, binary(n->op), 0, 0, 0, line);
        }
        break;
    default:
        break;
    }
}
static void statement(Emitter *e, Node *n) {
    int line = n->token.line;
    switch (n->kind) {
    case N_BLOCK:
        for (size_t i = 0; i < n->count; i++)
            statement(e, n->items[i]);
        break;
    case N_LET:
        if (n->type.length)
            emit(e, B_ALLOC, n->index, n->type.length, 0, line);
        else {
            if (n->a)
                expression(e, n->a);
            else
                emit(e, B_CONST, 0, 0, 0, line);
            emit(e, B_STORE, n->index, 0, 0, line);
        }
        break;
    case N_ASSIGN:
        if (n->a->kind == N_INDEX) {
            expression(e, n->a->a);
            expression(e, n->a->b);
            expression(e, n->b);
            emit(e, B_ASET, 0, 0, 0, line);
        } else {
            expression(e, n->b);
            emit(e, B_STORE, n->a->index, 0, 0, line);
        }
        break;
    case N_PRINT:
        expression(e, n->a);
        emit(e, B_PRINT, n->a->type.kind == TY_BOOL, 0, 0, line);
        break;
    case N_EXPR:
        expression(e, n->a);
        emit(e, B_POP, 0, 0, 0, line);
        break;
    case N_RETURN:
        if (n->a)
            expression(e, n->a);
        else
            emit(e, B_CONST, 0, 0, 0, line);
        emit(e, B_RET, 0, 0, 0, line);
        break;
    case N_IF: {
        expression(e, n->a);
        size_t otherwise = emit(e, B_JZ, 0, 0, 0, line);
        statement(e, n->b);
        size_t end = emit(e, B_JMP, 0, 0, 0, line);
        e->f->code[otherwise].a = (int)e->f->count;
        if (n->c)
            statement(e, n->c);
        e->f->code[end].a = (int)e->f->count;
        break;
    }
    case N_WHILE: {
        Loop loop = {e->f->count, NULL, 0, e->loop};
        e->loop = &loop;
        expression(e, n->a);
        size_t end = emit(e, B_JZ, 0, 0, 0, line);
        statement(e, n->b);
        emit(e, B_JMP, (int)loop.start, 0, 0, line);
        e->f->code[end].a = (int)e->f->count;
        for (size_t i = 0; i < loop.count; i++) {
            e->f->code[loop.breaks[i]].a = (int)e->f->count;
        }
        free(loop.breaks);
        e->loop = loop.parent;
        break;
    }
    case N_CONTINUE:
        emit(e, B_JMP, (int)e->loop->start, 0, 0, line);
        break;
    case N_BREAK: {
        size_t at = emit(e, B_JMP, 0, 0, 0, line);
        size_t *v = realloc(e->loop->breaks, (e->loop->count + 1) * sizeof(*v));
        if (!v)
            exit(2);
        e->loop->breaks = v;
        v[e->loop->count++] = at;
        break;
    }
    default:
        break;
    }
}
bool compile_direct(Node *root, Bytecode *out, const char *path) {
    (void)path;
    out->count = root->count;
    out->functions = calloc(root->count, sizeof(*out->functions));
    if (!out->functions)
        return false;
    for (size_t i = 0; i < root->count; i++) {
        Node *f = root->items[i];
        CodeFunction *cf = &out->functions[i];
        cf->name = f->name;
        cf->arity = (int)f->count;
        cf->slots = f->aux;
        Emitter e = {cf, NULL};
        statement(&e, f->a);
        emit(&e, B_CONST, 0, 0, 0, f->token.line);
        emit(&e, B_RET, 0, 0, 0, f->token.line);
        if (!strcmp(f->name, "main"))
            out->entry = (int)i;
    }
    return true;
}
