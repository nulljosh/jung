#include "interpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JUNG_VERSION "jung v1.0.0"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "jung: cannot open file '%s'\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    size_t nr = fread(buf, 1, (size_t)size, f);
    buf[nr] = '\0';
    fclose(f);
    return buf;
}

static void repl(void) {
    Interpreter it;
    interp_init(&it);

    printf("%s\n", JUNG_VERSION);
    printf("\"Until you make the unconscious conscious, it will direct\n");
    printf(" your life and you will call it fate.\" -- Carl Jung\n\n");
    printf("Type expressions or statements. Ctrl-D to exit.\n");

    char line[4096];
    while (1) {
        printf("jung> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        int len = (int)strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len == 0) continue;

        Lexer lex;
        lexer_init(&lex, line);
        lexer_tokenize(&lex);

        Parser parser;
        parser_init(&parser, lex.tokens, lex.token_count);
        ASTNode *program = parser_parse(&parser);

        if (program && program->type == NODE_PROGRAM && program->as.program.count > 0) {
            if (program->as.program.count == 1) {
                ASTNode *stmt = program->as.program.stmts[0];
                if (stmt->type == NODE_PRINT || stmt->type == NODE_ASSIGN ||
                    stmt->type == NODE_IF || stmt->type == NODE_WHILE ||
                    stmt->type == NODE_FOR || stmt->type == NODE_FUNC_DEF ||
                    stmt->type == NODE_CLASS || stmt->type == NODE_IMPORT ||
                    stmt->type == NODE_COMPOUND_ASSIGN || stmt->type == NODE_OBJ_ASSIGN) {
                    interp_exec(&it, program->as.program.stmts, program->as.program.count);
                } else {
                    Value v = interp_eval(&it, stmt);
                    if (v.type != VAL_NULL) {
                        char *s = val_to_string(v);
                        printf("%s\n", s);
                        free(s);
                    }
                    val_free(&v);
                }
            } else {
                interp_exec(&it, program->as.program.stmts, program->as.program.count);
            }
        }

        ast_free(program);
        lexer_free(&lex);
    }

    interp_free(&it);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        repl();
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("%s\n", JUNG_VERSION);
        return 0;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        printf("Usage: jung [options] [file]\n");
        printf("\n");
        printf("Options:\n");
        printf("  --version, -v    Print version\n");
        printf("  --help, -h       Print this help\n");
        printf("\n");
        printf("Run without arguments for interactive REPL.\n");
        printf("Run with a .jung, .jot, or .jit file to execute.\n");
        return 0;
    }

    char *source = read_file(argv[1]);
    Interpreter it;
    interp_init(&it);
    interp_run(&it, source);
    interp_free(&it);
    free(source);
    return 0;
}
