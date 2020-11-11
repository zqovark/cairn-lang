#include "cairn.h"
#include <string.h>

static Token peek(Parser *p) {
    return p->lex.tokens[p->position];
}
static Token advance(Parser *p) {
    Token t = peek(p);
    if (t.kind != T_EOF)
        p->position++;
    return t;
}
static bool match(Parser *p, TokenKind k) {
    if (peek(p).kind != k)
        return false;
    advance(p);
    return true;
}
static void error(Parser *p, Token t, const char *s) {
    diagnostic(p->lex.path, t, "%s", s);
    p->errors++;
}
static Token expect(Parser *p, TokenKind k) {
    Token t = peek(p);
    if (t.kind == k)
        return advance(p);
    diagnostic(p->lex.path, t, "expected '%s', found '%s'", token_name(k), token_name(t.kind));
    p->errors++;
    return t;
}
static Node *node(Parser *p, NodeKind k, Token t) {
    Node *n = arena_alloc(&p->arena, sizeof(*n));
    n->kind = k;
    n->token = t;
    n->index = -1;
    return n;
}
static void push(Parser *p, Node *n, Node *item) {
    Node **v = arena_alloc(&p->arena, (n->count + 1) * sizeof(*v));
    if (n->count)
        memcpy(v, n->items, n->count * sizeof(*v));
    v[n->count++] = item;
    n->items = v;
}
static char *identifier(Parser *p, Token t) {
    return arena_string(&p->arena, t.text, t.length);
}
static Type parse_type(Parser *p, bool array) {
    Token t = advance(p);
    Type ty = {TY_VOID, 0};
    if (t.kind == T_TYPE_INT)
        ty.kind = TY_INT;
    else if (t.kind == T_TYPE_BOOL)
        ty.kind = TY_BOOL;
    else if (t.kind != T_TYPE_VOID)
        error(p, t, "expected type int, bool, or void");
    if (array && match(p, T_LS)) {
        Token size = expect(p, T_INT);
        if (size.kind != T_INT || size.value < 1 || size.value > 65536)
            error(p, size, "array length must be between 1 and 65536");
        else
            ty.length = (int)size.value;
        expect(p, T_RS);
    }
    return ty;
}
static int precedence(TokenKind k) {
    switch (k) {
    case T_OR:
        return 1;
    case T_AND:
        return 2;
    case T_EQEQ:
    case T_NE:
        return 3;
    case T_LT:
    case T_LE:
    case T_GT:
    case T_GE:
        return 4;
    case T_PLUS:
    case T_MINUS:
        return 5;
    case T_STAR:
    case T_SLASH:
    case T_PERCENT:
        return 6;
    default:
        return 0;
    }
}
static Node *expression(Parser *, int, int);
static Node *primary(Parser *p, int depth) {
    Token t = advance(p);
    Node *n;
    if (t.kind == T_INT || t.kind == T_TRUE || t.kind == T_FALSE) {
        n = node(p, t.kind == T_INT ? N_INT : N_BOOL, t);
        n->value = t.kind == T_INT ? t.value : t.kind == T_TRUE;
    } else if (t.kind == T_ID) {
        n = node(p, N_VAR, t);
        n->name = identifier(p, t);
    } else if (t.kind == T_LP) {
        n = expression(p, 1, depth + 1);
        expect(p, T_RP);
    } else if (t.kind == T_MINUS || t.kind == T_NOT) {
        n = node(p, N_UNARY, t);
        n->op = t.kind;
        n->a = expression(p, 7, depth + 1);
    } else {
        error(p, t, "expected expression");
        n = node(p, N_INT, t);
    }
    int postfix = 0;
    while (peek(p).kind == T_LP || peek(p).kind == T_LS) {
        if (++postfix > 128) {
            error(p, t, "postfix nesting limit exceeded");
            break;
        }
        if (match(p, T_LP)) {
            Node *call = node(p, N_CALL, t);
            if (n->kind != N_VAR)
                error(p, t, "only named functions can be called");
            call->name = n->name;
            if (peek(p).kind != T_RP) {
                do {
                    push(p, call, expression(p, 1, depth + 1));
                    if (call->count > 16) {
                        error(p, t, "at most 16 arguments are supported");
                        break;
                    }
                } while (match(p, T_COMMA));
            }
            expect(p, T_RP);
            n = call;
        } else {
            advance(p);
            Node *idx = node(p, N_INDEX, t);
            idx->a = n;
            idx->b = expression(p, 1, depth + 1);
            expect(p, T_RS);
            n = idx;
        }
    }
    return n;
}
static Node *expression(Parser *p, int min, int depth) {
    if (depth > 128) {
        error(p, peek(p), "expression nesting limit exceeded");
        return node(p, N_INT, peek(p));
    }
    Node *left = primary(p, depth);
    int operators = 0;
    while (precedence(peek(p).kind) >= min) {
        if (++operators > 256) {
            error(p, peek(p), "expression operator limit exceeded");
            break;
        }
        Token op = advance(p);
        Node *n = node(p, N_BINARY, op);
        n->op = op.kind;
        n->a = left;
        n->b = expression(p, precedence(op.kind) + 1, depth + 1);
        left = n;
    }
    return left;
}
static Node *block(Parser *, int);
static Node *statement(Parser *p, int depth) {
    Token t = peek(p);
    Node *n;
    if (t.kind == T_LB)
        return block(p, depth + 1);
    if (match(p, T_LET)) {
        n = node(p, N_LET, t);
        Token name = expect(p, T_ID);
        n->name = identifier(p, name);
        expect(p, T_COLON);
        n->type = parse_type(p, true);
        if (match(p, T_EQ))
            n->a = expression(p, 1, 0);
        expect(p, T_SEMI);
        return n;
    }
    if (match(p, T_IF)) {
        n = node(p, N_IF, t);
        n->a = expression(p, 1, 0);
        n->b = block(p, depth + 1);
        if (match(p, T_ELSE))
            n->c = peek(p).kind == T_IF ? statement(p, depth + 1) : block(p, depth + 1);
        return n;
    }
    if (match(p, T_WHILE)) {
        n = node(p, N_WHILE, t);
        n->a = expression(p, 1, 0);
        n->b = block(p, depth + 1);
        return n;
    }
    if (match(p, T_RETURN)) {
        n = node(p, N_RETURN, t);
        if (peek(p).kind != T_SEMI)
            n->a = expression(p, 1, 0);
        expect(p, T_SEMI);
        return n;
    }
    if (match(p, T_PRINT)) {
        n = node(p, N_PRINT, t);
        expect(p, T_LP);
        n->a = expression(p, 1, 0);
        expect(p, T_RP);
        expect(p, T_SEMI);
        return n;
    }
    if (match(p, T_BREAK) || match(p, T_CONTINUE)) {
        n = node(p, t.kind == T_BREAK ? N_BREAK : N_CONTINUE, t);
        expect(p, T_SEMI);
        return n;
    }
    Node *e = expression(p, 1, 0);
    if (match(p, T_EQ)) {
        n = node(p, N_ASSIGN, t);
        n->a = e;
        n->b = expression(p, 1, 0);
    } else {
        n = node(p, N_EXPR, t);
        n->a = e;
    }
    expect(p, T_SEMI);
    return n;
}
static Node *block(Parser *p, int depth) {
    Token t = expect(p, T_LB);
    Node *n = node(p, N_BLOCK, t);
    if (t.kind != T_LB)
        return n;
    if (depth > 128) {
        error(p, t, "block nesting limit exceeded");
        return n;
    }
    while (peek(p).kind != T_RB && peek(p).kind != T_EOF) {
        if (n->count >= 1024) {
            error(p, peek(p), "block statement limit exceeded");
            while (peek(p).kind != T_RB && peek(p).kind != T_EOF)
                advance(p);
            break;
        }
        size_t start = p->position;
        int before = p->errors;
        push(p, n, statement(p, depth));
        if (p->errors > before) {
            while (peek(p).kind != T_EOF && peek(p).kind != T_RB && peek(p).kind != T_SEMI)
                advance(p);
            match(p, T_SEMI);
        }
        if (p->position == start)
            advance(p);
    }
    expect(p, T_RB);
    return n;
}
Node *parse_program(Parser *p, const char *source, const char *path) {
    p->lex.source = source;
    p->lex.path = path;
    if (!lex_source(&p->lex))
        return NULL;
    Node *root = node(p, N_PROGRAM, peek(p));
    while (peek(p).kind != T_EOF) {
        Token t = expect(p, T_FN);
        if (t.kind != T_FN) {
            while (peek(p).kind != T_FN && peek(p).kind != T_EOF)
                advance(p);
            continue;
        }
        Node *f = node(p, N_FUNCTION, t);
        Token name = expect(p, T_ID);
        f->name = identifier(p, name);
        expect(p, T_LP);
        if (peek(p).kind != T_RP) {
            do {
                Token id = expect(p, T_ID);
                Node *arg = node(p, N_PARAM, id);
                arg->name = identifier(p, id);
                expect(p, T_COLON);
                arg->type = parse_type(p, false);
                push(p, f, arg);
                if (f->count > 16) {
                    error(p, id, "at most 16 parameters are supported");
                    break;
                }
            } while (match(p, T_COMMA));
        }
        expect(p, T_RP);
        expect(p, T_ARROW);
        f->type = parse_type(p, false);
        f->a = block(p, 0);
        push(p, root, f);
        if (root->count > 256) {
            error(p, t, "at most 256 functions are supported");
            break;
        }
    }
    return p->errors ? NULL : root;
}
void parser_free(Parser *p) {
    lex_free(&p->lex);
    arena_free(&p->arena);
}
void dump_ast(const Node *n, FILE *out, int depth) {
    if (!n)
        return;
    static const char *names[] = {"program", "function", "param", "block",  "let",
                                  "assign",  "if",       "while", "return", "print",
                                  "break",   "continue", "expr",  "int",    "bool",
                                  "var",     "binary",   "unary", "call",   "index"};
    fprintf(out, "%*s%s", depth * 2, "", names[n->kind]);
    if (n->name)
        fprintf(out, " %s", n->name);
    if (n->kind == N_INT || n->kind == N_BOOL)
        fprintf(out, " %lld", (long long)n->value);
    if (n->kind == N_BINARY || n->kind == N_UNARY)
        fprintf(out, " %s", token_name(n->op));
    if (n->kind == N_FUNCTION || n->kind == N_PARAM || n->kind == N_LET) {
        fprintf(out, " :%s", type_name(n->type));
        if (n->type.length)
            fprintf(out, "[%d]", n->type.length);
    }
    fputc('\n', out);
    for (size_t i = 0; i < n->count; i++) {
        dump_ast(n->items[i], out, depth + 1);
    }
    dump_ast(n->a, out, depth + 1);
    dump_ast(n->b, out, depth + 1);
    dump_ast(n->c, out, depth + 1);
}
