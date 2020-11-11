#include "cairn.h"
#include <string.h>

typedef struct {
    const char *name;
    Type type;
    int slot, scope;
} Binding;
typedef struct {
    Node *program, *function;
    const char *path;
    Binding vars[2048];
    int count, next, scope, loops, errors;
} Context;
static void fail(Context *c, Node *n, const char *message) {
    diagnostic(c->path, n->token, "%s", message);
    c->errors++;
}
static bool same(Type a, Type b) {
    return a.kind == b.kind && a.length == b.length;
}
static Type scalar(TypeKind k) {
    return (Type){k, 0};
}
static Binding *lookup(Context *c, const char *name) {
    for (int i = c->count - 1; i >= 0; i--)
        if (!strcmp(c->vars[i].name, name))
            return &c->vars[i];
    return NULL;
}
static void bind(Context *c, Node *n) {
    for (int i = c->count - 1; i >= 0 && c->vars[i].scope == c->scope; i--)
        if (!strcmp(c->vars[i].name, n->name)) {
            fail(c, n, "duplicate variable in this scope");
            return;
        }
    if (c->count >= 2048 || c->next >= 2048) {
        fail(c, n, "local variable limit exceeded");
        return;
    }
    n->index = c->next++;
    c->vars[c->count++] = (Binding){n->name, n->type, n->index, c->scope};
}
static Type expr(Context *c, Node *n) {
    Type a, b;
    n->type = scalar(TY_INT);
    switch (n->kind) {
    case N_INT:
        break;
    case N_BOOL:
        n->type = scalar(TY_BOOL);
        break;
    case N_VAR: {
        Binding *v = lookup(c, n->name);
        if (!v)
            fail(c, n, "undeclared variable");
        else {
            n->index = v->slot;
            n->type = v->type;
        }
        break;
    }
    case N_INDEX:
        a = expr(c, n->a);
        b = expr(c, n->b);
        if (!a.length)
            fail(c, n, "indexing requires an array");
        if (!same(b, scalar(TY_INT)))
            fail(c, n, "array index must be int");
        n->type = scalar(a.kind);
        break;
    case N_UNARY:
        a = expr(c, n->a);
        n->type = scalar(n->op == T_NOT ? TY_BOOL : TY_INT);
        if (!same(a, n->type))
            fail(c, n, "unary operand has incompatible type");
        break;
    case N_BINARY:
        a = expr(c, n->a);
        b = expr(c, n->b);
        if (n->op == T_AND || n->op == T_OR) {
            n->type = scalar(TY_BOOL);
            if (!same(a, n->type) || !same(b, n->type))
                fail(c, n, "logical operands must be bool");
        } else if (n->op == T_EQEQ || n->op == T_NE) {
            n->type = scalar(TY_BOOL);
            if (a.length || b.length || a.kind == TY_VOID || !same(a, b))
                fail(c, n, "equality operands must have the same scalar type");
        } else {
            if (!same(a, scalar(TY_INT)) || !same(b, scalar(TY_INT)))
                fail(c, n, "arithmetic and ordered comparison operands must be int");
            n->type =
                scalar(n->op == T_LT || n->op == T_LE || n->op == T_GT || n->op == T_GE ? TY_BOOL
                                                                                        : TY_INT);
        }
        break;
    case N_CALL: {
        Node *f = NULL;
        for (size_t i = 0; i < c->program->count; i++)
            if (n->name && !strcmp(n->name, c->program->items[i]->name)) {
                f = c->program->items[i];
                break;
            }
        if (!f)
            fail(c, n, "unknown function");
        else {
            n->index = f->index;
            n->type = f->type;
            if (f->count != n->count)
                fail(c, n, "argument count does not match function signature");
        }
        for (size_t i = 0; i < n->count; i++) {
            a = expr(c, n->items[i]);
            if (f && i < f->count && !same(a, f->items[i]->type))
                fail(c, n->items[i], "argument type does not match parameter");
        }
        break;
    }
    default:
        fail(c, n, "invalid expression node");
        break;
    }
    return n->type;
}
static bool stmt(Context *, Node *);
static bool statements(Context *c, Node *n, bool scope) {
    int saved = c->count;
    if (scope)
        c->scope++;
    bool returns = false;
    for (size_t i = 0; i < n->count; i++) {
        bool r = stmt(c, n->items[i]);
        returns = returns || r;
    }
    if (scope) {
        c->count = saved;
        c->scope--;
    }
    return returns;
}
static bool stmt(Context *c, Node *n) {
    Type a, b;
    switch (n->kind) {
    case N_BLOCK:
        return statements(c, n, true);
    case N_LET:
        if (n->type.kind == TY_VOID)
            fail(c, n, "variables cannot have type void");
        if (n->a) {
            a = expr(c, n->a);
            if (n->type.length)
                fail(c, n, "array initializers are not supported");
            else if (!same(a, n->type))
                fail(c, n, "initializer type does not match variable");
        }
        bind(c, n);
        break;
    case N_ASSIGN:
        a = expr(c, n->a);
        b = expr(c, n->b);
        if (n->a->kind != N_VAR && n->a->kind != N_INDEX)
            fail(c, n, "assignment target is not writable");
        if (a.length) {
            fail(c, n, "whole-array assignment is not supported");
        }
        if (!same(a, b)) {
            fail(c, n, "assignment types do not match");
        }
        break;
    case N_IF: {
        a = expr(c, n->a);
        if (!same(a, scalar(TY_BOOL)))
            fail(c, n, "condition must be bool");
        bool l = stmt(c, n->b), r = n->c ? stmt(c, n->c) : false;
        return l && r;
    }
    case N_WHILE:
        a = expr(c, n->a);
        if (!same(a, scalar(TY_BOOL)))
            fail(c, n, "condition must be bool");
        c->loops++;
        stmt(c, n->b);
        c->loops--;
        break;
    case N_RETURN:
        a = n->a ? expr(c, n->a) : scalar(TY_VOID);
        if (!same(a, c->function->type))
            fail(c, n, "return type does not match function");
        return true;
    case N_PRINT:
        a = expr(c, n->a);
        if (a.kind == TY_VOID || a.length)
            fail(c, n, "print requires a scalar value");
        break;
    case N_EXPR:
        a = expr(c, n->a);
        if (a.length)
            fail(c, n, "array cannot be used as a standalone expression");
        break;
    case N_BREAK:
    case N_CONTINUE:
        if (!c->loops)
            fail(c, n, "loop control used outside a loop");
        break;
    default:
        fail(c, n, "invalid statement node");
        break;
    }
    return false;
}
bool analyze(Node *program, const char *path) {
    Context c = {0};
    c.program = program;
    c.path = path;
    bool main_found = false;
    for (size_t i = 0; i < program->count; i++) {
        Node *f = program->items[i];
        f->index = (int)i;
        for (size_t j = 0; j < i; j++)
            if (!strcmp(f->name, program->items[j]->name))
                fail(&c, f, "duplicate function");
        if (!strcmp(f->name, "main")) {
            main_found = true;
            if (f->count || !same(f->type, scalar(TY_INT)))
                fail(&c, f, "main must have signature fn main() -> int");
        }
        for (size_t j = 0; j < f->count; j++)
            if (f->items[j]->type.kind == TY_VOID)
                fail(&c, f->items[j], "parameter cannot have type void");
    }
    if (!main_found) {
        diagnostic(path, program->token, "missing main function");
        c.errors++;
    }
    for (size_t i = 0; i < program->count; i++) {
        Node *f = program->items[i];
        c.function = f;
        c.count = c.next = c.scope = c.loops = 0;
        for (size_t j = 0; j < f->count; j++)
            bind(&c, f->items[j]);
        bool returns = statements(&c, f->a, false);
        if (f->type.kind != TY_VOID && !returns)
            fail(&c, f, "non-void function may fall through without returning");
        f->aux = c.next;
    }
    return c.errors == 0;
}
