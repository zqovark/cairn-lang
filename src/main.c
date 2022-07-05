#include "cairn.h"
#include "ir.h"
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
    const char *mode = "--run", *path = NULL, *output = NULL;
    uint64_t budget = 1000000;
    bool optimize = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) {
            puts("cairn 0.3.0 (reconstructed teaching history)");
            return 0;
        }
        if (!strcmp(argv[i], "--no-opt")) {
            optimize = false;
            continue;
        }
        if (!strcmp(argv[i], "--budget")) {
            if (++i >= argc)
                return 2;
            char *end;
            budget = strtoull(argv[i], &end, 10);
            if (!budget || *end)
                return 2;
        } else if (!strcmp(argv[i], "-o")) {
            if (++i >= argc)
                return 2;
            output = argv[i];
        } else if (argv[i][0] == '-')
            mode = argv[i];
        else if (!path)
            path = argv[i];
        else
            return 2;
    }
    if (!path) {
        fputs("usage: cairn "
              "[--run|--check|--tokens|--ast|--parse|--bytecode|--ir|--cfg|--stats|--emit|--exec] "
              "[-o "
              "output] [--budget steps] file\n",
              stderr);
        return 2;
    }
    Bytecode b = {0};
    IRProgram ir = {0};
    Parser p = {0};
    char *source = NULL;
    int status = 0;
    if (!strcmp(mode, "--exec")) {
        if (!bytecode_read(&b, path))
            return 1;
        int64_t value;
        if (!vm_run(&b, stdout, stderr, budget, &value))
            status = 1;
        bytecode_free(&b);
        return status;
    }
    source = read_source(path);
    if (!source)
        return 2;
    if (!strcmp(mode, "--tokens")) {
        p.lex.source = source;
        p.lex.path = path;
        if (!lex_source(&p.lex))
            status = 1;
        else
            for (size_t i = 0; i < p.lex.count; i++) {
                Token t = p.lex.tokens[i];
                printf("%d:%d %-12s %.*s\n", t.line, t.column, token_name(t.kind), (int)t.length,
                       t.text);
            }
        goto done;
    }
    Node *root = parse_program(&p, source, path);
    if (!root) {
        status = 1;
        goto done;
    }
    if (!strcmp(mode, "--ast")) {
        dump_ast(root, stdout, 0);
        goto done;
    }
    if (!strcmp(mode, "--parse"))
        goto done;
    if (!analyze(root, path)) {
        status = 1;
        goto done;
    }
    if (!strcmp(mode, "--check"))
        goto done;
    if (strcmp(mode, "--run") && strcmp(mode, "--bytecode") && strcmp(mode, "--emit") &&
        strcmp(mode, "--ir") && strcmp(mode, "--cfg") && strcmp(mode, "--stats")) {
        fprintf(stderr, "unknown mode: %s\n", mode);
        status = 2;
        goto done;
    }
    if (!ir_build(root, &ir, path)) {
        status = 1;
        goto done;
    }
    if (optimize)
        ir_optimize(&ir);
    if (!strcmp(mode, "--ir")) {
        ir_dump(&ir, stdout);
        goto done;
    }
    if (!strcmp(mode, "--cfg")) {
        flow_dump(&ir, stdout);
        goto done;
    }
    if (!ir_lower(&ir, &b) || !bytecode_verify(&b, stderr)) {
        status = 1;
        goto done;
    }
    if (!strcmp(mode, "--stats")) {
        for (size_t fi = 0; fi < ir.count; fi++) {
            size_t active = 0;
            for (size_t pc = 0; pc < ir.functions[fi].count; pc++)
                active += ir.functions[fi].code[pc].op != B_NOP;
            printf("%s: ir=%zu bytecode=%zu\n", ir.functions[fi].name, active,
                   b.functions[fi].count);
        }
        goto done;
    }
    if (!strcmp(mode, "--bytecode"))
        bytecode_dump(&b, stdout);
    else if (!strcmp(mode, "--emit")) {
        if (!output) {
            fputs("--emit requires -o output\n", stderr);
            status = 2;
        } else if (!bytecode_write(&b, output))
            status = 1;
    } else {
        int64_t value;
        if (!vm_run(&b, stdout, stderr, budget, &value))
            status = 1;
    }
done:
    bytecode_free(&b);
    ir_free(&ir);
    parser_free(&p);
    free(source);
    return status;
}
