#include "cairn.h"
#include <stdlib.h>
#include <string.h>

static char *read_source(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long length = ftell(f);
    if (length < 0 || length > 1024 * 1024) {
        fputs("cairn: source must not exceed 1 MiB\n", stderr);
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *s = malloc((size_t)length + 1);
    if (!s) {
        fclose(f);
        return NULL;
    }
    if (fread(s, 1, (size_t)length, f) != (size_t)length) {
        free(s);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (memchr(s, 0, (size_t)length)) {
        free(s);
        fputs("cairn: embedded NUL in source\n", stderr);
        return NULL;
    }
    s[length] = 0;
    return s;
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        puts("cairn 0.1.0 (reconstructed teaching history)");
        return 0;
    }
    if (argc != 3 ||
        (strcmp(argv[1], "--tokens") && strcmp(argv[1], "--ast") && strcmp(argv[1], "--parse"))) {
        fputs("usage: cairn --tokens|--ast|--parse source.crn\n", stderr);
        return 2;
    }
    char *source = read_source(argv[2]);
    if (!source)
        return 2;
    Parser p = {0};
    int status = 0;
    if (!strcmp(argv[1], "--tokens")) {
        p.lex.source = source;
        p.lex.path = argv[2];
        if (!lex_source(&p.lex))
            status = 1;
        else
            for (size_t i = 0; i < p.lex.count; i++) {
                Token t = p.lex.tokens[i];
                printf("%d:%d %-12s %.*s\n", t.line, t.column, token_name(t.kind), (int)t.length,
                       t.text);
            }
    } else {
        Node *root = parse_program(&p, source, argv[2]);
        if (!root)
            status = 1;
        else if (!strcmp(argv[1], "--ast"))
            dump_ast(root, stdout, 0);
    }
    parser_free(&p);
    free(source);
    return status;
}
