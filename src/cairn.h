#ifndef CAIRN_H
#define CAIRN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    T_EOF,
    T_ID,
    T_INT,
    T_FN,
    T_LET,
    T_IF,
    T_ELSE,
    T_WHILE,
    T_RETURN,
    T_PRINT,
    T_TRUE,
    T_FALSE,
    T_TYPE_INT,
    T_TYPE_BOOL,
    T_TYPE_VOID,
    T_BREAK,
    T_CONTINUE,
    T_LP,
    T_RP,
    T_LB,
    T_RB,
    T_LS,
    T_RS,
    T_COLON,
    T_SEMI,
    T_COMMA,
    T_ARROW,
    T_PLUS,
    T_MINUS,
    T_STAR,
    T_SLASH,
    T_PERCENT,
    T_EQ,
    T_EQEQ,
    T_NE,
    T_LT,
    T_LE,
    T_GT,
    T_GE,
    T_AND,
    T_OR,
    T_NOT
} TokenKind;
typedef struct {
    TokenKind kind;
    const char *text;
    size_t length;
    int line, column;
    int64_t value;
} Token;
typedef struct {
    const char *source, *path;
    Token *tokens;
    size_t count, capacity;
    int errors;
} Lexer;
typedef struct {
    void **items;
    size_t count, capacity;
} Arena;
typedef enum { TY_INT, TY_BOOL, TY_VOID } TypeKind;
typedef struct {
    TypeKind kind;
    int length;
} Type;
typedef enum {
    N_PROGRAM,
    N_FUNCTION,
    N_PARAM,
    N_BLOCK,
    N_LET,
    N_ASSIGN,
    N_IF,
    N_WHILE,
    N_RETURN,
    N_PRINT,
    N_BREAK,
    N_CONTINUE,
    N_EXPR,
    N_INT,
    N_BOOL,
    N_VAR,
    N_BINARY,
    N_UNARY,
    N_CALL,
    N_INDEX
} NodeKind;
typedef struct Node Node;
struct Node {
    NodeKind kind;
    Token token;
    Type type;
    char *name;
    int64_t value;
    TokenKind op;
    Node *a, *b, *c;
    Node **items;
    size_t count;
    int index, aux;
};
typedef struct {
    Lexer lex;
    Arena arena;
    size_t position;
    int errors;
} Parser;
void *arena_alloc(Arena *, size_t);
char *arena_string(Arena *, const char *, size_t);
void arena_free(Arena *);
void diagnostic(const char *, Token, const char *, ...);
const char *token_name(TokenKind);
bool lex_source(Lexer *);
void lex_free(Lexer *);
Node *parse_program(Parser *, const char *, const char *);
void parser_free(Parser *);
void dump_ast(const Node *, FILE *, int);
bool analyze(Node *, const char *);
const char *type_name(Type);

typedef enum {
    B_CONST,
    B_LOAD,
    B_STORE,
    B_ADD,
    B_SUB,
    B_MUL,
    B_DIV,
    B_MOD,
    B_EQ,
    B_NE,
    B_LT,
    B_LE,
    B_GT,
    B_GE,
    B_NOT,
    B_NEG,
    B_JMP,
    B_JZ,
    B_CALL,
    B_RET,
    B_PRINT,
    B_ALLOC,
    B_AGET,
    B_ASET,
    B_POP,
    B_NOP
} Op;
typedef struct {
    Op op;
    int a, b;
    int64_t value;
    int line;
} Instruction;
typedef struct {
    char *name;
    int arity, slots;
    Instruction *code;
    size_t count, capacity;
} CodeFunction;
typedef struct {
    CodeFunction *functions;
    size_t count;
    int entry;
    bool owns_names;
} Bytecode;
const char *op_name(Op);
bool compile_direct(Node *, Bytecode *, const char *);
void bytecode_free(Bytecode *);
void bytecode_dump(const Bytecode *, FILE *);
bool bytecode_verify(const Bytecode *, FILE *);
bool bytecode_write(const Bytecode *, const char *);
bool bytecode_read(Bytecode *, const char *);
bool vm_run(const Bytecode *, FILE *, FILE *, uint64_t, int64_t *);
bool checked_binary(Op, int64_t, int64_t, int64_t *);

#endif
