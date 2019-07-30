#include "cairn.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const char *names[] = {
    "eof",   "identifier", "integer", "fn",  "let",  "if",   "else",  "while",    "return",
    "print", "true",       "false",   "int", "bool", "void", "break", "continue", "(",
    ")",     "{",          "}",       "[",   "]",    ":",    ";",     ",",        "->",
    "+",     "-",          "*",       "/",   "%",    "=",    "==",    "!=",       "<",
    "<=",    ">",          ">=",      "&&",  "||",   "!"};
const char *token_name(TokenKind k) {
    return names[k];
}
static bool alpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool digit(unsigned char c) {
    return c >= '0' && c <= '9';
}
static void append(Lexer *l, Token t) {
    if (l->count == l->capacity) {
        l->capacity = l->capacity ? l->capacity * 2 : 64;
        Token *p = realloc(l->tokens, l->capacity * sizeof(*p));
        if (!p) {
            fputs("cairn: out of memory\n", stderr);
            exit(2);
        }
        l->tokens = p;
    }
    l->tokens[l->count++] = t;
}
bool lex_source(Lexer *l) {
    const char *p = l->source;
    int line = 1, column = 1;
    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            if (*p == '\n') {
                line++;
                column = 1;
            } else
                column++;
            p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') {
                p++;
                column++;
            }
            continue;
        }
        Token t = {T_EOF, p, 1, line, column, 0};
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            column += 2;
            int depth = 1;
            while (*p && depth) {
                if (p[0] == '/' && p[1] == '*') {
                    depth++;
                    p += 2;
                    column += 2;
                } else if (p[0] == '*' && p[1] == '/') {
                    depth--;
                    p += 2;
                    column += 2;
                } else {
                    if (*p == '\n') {
                        line++;
                        column = 1;
                    } else
                        column++;
                    p++;
                }
            }
            if (depth) {
                diagnostic(l->path, t, "unterminated block comment");
                l->errors++;
            }
            continue;
        }
        if (alpha((unsigned char)*p)) {
            do {
                p++;
                column++;
            } while (alpha((unsigned char)*p) || digit((unsigned char)*p));
            t.length = (size_t)(p - t.text);
            t.kind = T_ID;
            for (int k = T_FN; k <= T_CONTINUE; k++)
                if (strlen(names[k]) == t.length && !memcmp(names[k], t.text, t.length)) {
                    t.kind = (TokenKind)k;
                    break;
                }
        } else if (digit((unsigned char)*p)) {
            t.kind = T_INT;
            bool overflow = false;
            do {
                int d = *p - '0';
                if (t.value > (INT64_MAX - d) / 10)
                    overflow = true;
                else if (!overflow)
                    t.value = t.value * 10 + d;
                p++;
                column++;
            } while (digit((unsigned char)*p));
            t.length = (size_t)(p - t.text);
            if (overflow) {
                diagnostic(l->path, t, "integer literal exceeds signed 64-bit range");
                l->errors++;
            }
        } else {
            struct Pair {
                const char *s;
                TokenKind k;
            } pairs[] = {{"->", T_ARROW}, {"==", T_EQEQ}, {"!=", T_NE}, {"<=", T_LE},
                         {">=", T_GE},    {"&&", T_AND},  {"||", T_OR}};
            bool found = false;
            for (size_t i = 0; i < sizeof(pairs) / sizeof(*pairs); i++)
                if (p[0] == pairs[i].s[0] && p[1] == pairs[i].s[1]) {
                    t.kind = pairs[i].k;
                    t.length = 2;
                    found = true;
                    break;
                }
            if (!found) {
                const char *single = "(){}[]:;,+-*/%=<>!";
                TokenKind ks[] = {T_LP,    T_RP,      T_LB,    T_RB,   T_LS,    T_RS,
                                  T_COLON, T_SEMI,    T_COMMA, T_PLUS, T_MINUS, T_STAR,
                                  T_SLASH, T_PERCENT, T_EQ,    T_LT,   T_GT,    T_NOT};
                const char *q = strchr(single, *p);
                if (q)
                    t.kind = ks[q - single];
                else {
                    diagnostic(l->path, t, "unexpected byte 0x%02x", (unsigned char)*p);
                    l->errors++;
                    p++;
                    column++;
                    continue;
                }
            }
            p += t.length;
            column += (int)t.length;
        }
        append(l, t);
    }
    append(l, (Token){T_EOF, p, 0, line, column, 0});
    return l->errors == 0;
}
void lex_free(Lexer *l) {
    free(l->tokens);
    l->tokens = NULL;
    l->count = l->capacity = 0;
}
