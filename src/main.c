#include "interpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JUNG_VERSION "jung v0.1.0"

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

int main(int argc, char **argv) {
    if (argc < 2) {
        Interpreter interp;
        interpreter_init(&interp);
        interpreter_repl(&interp);
        interpreter_free(&interp);
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
        printf("Run with a file path to execute a script.\n");
        return 0;
    }

    char *source = read_file(argv[1]);
    Interpreter interp;
    interpreter_init(&interp);
    interpreter_run_source(&interp, source);
    interpreter_free(&interp);
    free(source);
    return 0;
}
