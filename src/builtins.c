#include "builtins.h"
#include "interpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>

/* ---- builtin functions ---- */

/* str(x) - convert anything to string */
static Value bi_str(Value *args, int argc) {
    if (argc < 1) return val_string("", 0);
    char *s = val_to_string(args[0]);
    int len = (int)strlen(s);
    return val_string_take(s, len);
}

/* len(x) - length of string or array */
static Value bi_len(Value *args, int argc) {
    if (argc < 1) return val_number(0);
    if (args[0].type == VAL_STRING) return val_number(args[0].as.string.len);
    if (args[0].type == VAL_ARRAY) return val_number(args[0].as.array.count);
    return val_number(0);
}

/* push(arr, item) */
static Value bi_push(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_ARRAY) return val_null();
    val_array_push(&args[0], val_copy(args[1]));
    return val_null();
}

/* pop(arr) */
static Value bi_pop(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_ARRAY) return val_null();
    return val_array_pop(&args[0]);
}

/* range(n) or range(start, end) */
static Value bi_range(Value *args, int argc) {
    if (argc < 1) return val_array(8);
    int start = 0, end = 0;
    if (argc == 1) {
        end = (int)args[0].as.number;
    } else {
        start = (int)args[0].as.number;
        end = (int)args[1].as.number;
    }
    Value arr = val_array(end - start > 0 ? end - start : 8);
    for (int i = start; i < end; i++) {
        val_array_push(&arr, val_number(i));
    }
    return arr;
}

/* int(x) - convert to integer */
static Value bi_int(Value *args, int argc) {
    if (argc < 1) return val_number(0);
    if (args[0].type == VAL_NUMBER) return val_number(floor(args[0].as.number));
    if (args[0].type == VAL_STRING) {
        char *end;
        double val = strtod(args[0].as.string.str, &end);
        if (end == args[0].as.string.str) return val_number(0);
        return val_number(val);
    }
    if (args[0].type == VAL_BOOL) return val_number(args[0].as.boolean);
    return val_number(0);
}

/* float(x) - convert to float */
static Value bi_float(Value *args, int argc) {
    if (argc < 1) return val_number(0);
    if (args[0].type == VAL_NUMBER) return val_number(args[0].as.number);
    if (args[0].type == VAL_STRING) {
        char *end;
        double val = strtod(args[0].as.string.str, &end);
        if (end == args[0].as.string.str) return val_number(0);
        return val_number(val);
    }
    return val_number(0);
}

/* input(prompt) - read line from stdin */
static Value bi_input(Value *args, int argc) {
    if (argc > 0 && args[0].type == VAL_STRING) {
        printf("%s", args[0].as.string.str);
        fflush(stdout);
    }
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return val_string("", 0);
    int len = (int)strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
    return val_string(buf, len);
}

/* split(str, delim) */
static Value bi_split(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_array(8);
    const char *s = args[0].as.string.str;
    const char *d = args[1].as.string.str;
    int dlen = (int)strlen(d);
    Value arr = val_array(8);

    if (dlen == 0) {
        /* split every character */
        for (int i = 0; i < args[0].as.string.len; i++) {
            val_array_push(&arr, val_string(s + i, 1));
        }
        return arr;
    }

    const char *start = s;
    while (1) {
        const char *found = strstr(start, d);
        if (!found) {
            val_array_push(&arr, val_string(start, (int)strlen(start)));
            break;
        }
        val_array_push(&arr, val_string(start, (int)(found - start)));
        start = found + dlen;
    }
    return arr;
}

/* join(arr, sep) */
static Value bi_join(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_ARRAY || args[1].type != VAL_STRING)
        return val_string("", 0);
    const char *sep = args[1].as.string.str;
    int seplen = (int)strlen(sep);
    int cap = 256, len = 0;
    char *buf = malloc((size_t)cap);
    buf[0] = '\0';

    for (int i = 0; i < args[0].as.array.count; i++) {
        if (i > 0) {
            while (len + seplen + 1 >= cap) { cap *= 2; buf = realloc(buf, (size_t)cap); }
            memcpy(buf + len, sep, (size_t)seplen);
            len += seplen;
        }
        char *s = val_to_string(args[0].as.array.items[i]);
        int slen = (int)strlen(s);
        while (len + slen + 1 >= cap) { cap *= 2; buf = realloc(buf, (size_t)cap); }
        memcpy(buf + len, s, (size_t)slen);
        len += slen;
        free(s);
    }
    buf[len] = '\0';
    return val_string_take(buf, len);
}

/* keys(obj) */
static Value bi_keys(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_OBJECT) return val_array(8);
    return table_keys(args[0].as.object);
}

/* values(obj) */
static Value bi_values(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_OBJECT) return val_array(8);
    return table_values(args[0].as.object);
}

/* has(obj, key) */
static Value bi_has(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_OBJECT || args[1].type != VAL_STRING)
        return val_bool(0);
    return val_bool(table_has(args[0].as.object, args[1].as.string.str));
}

/* delete(obj, key) */
static Value bi_delete(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_OBJECT || args[1].type != VAL_STRING)
        return val_null();
    table_delete(args[0].as.object, args[1].as.string.str);
    return val_null();
}

/* slice(str_or_arr, start, end) */
static Value bi_slice(Value *args, int argc) {
    if (argc < 2) return val_null();
    if (args[0].type == VAL_STRING) {
        int len = args[0].as.string.len;
        int start = (int)args[1].as.number;
        int end = (argc >= 3) ? (int)args[2].as.number : len;
        if (start < 0) start += len;
        if (end < 0) end += len;
        if (start < 0) start = 0;
        if (end > len) end = len;
        if (start >= end) return val_string("", 0);
        return val_string(args[0].as.string.str + start, end - start);
    }
    if (args[0].type == VAL_ARRAY) {
        int len = args[0].as.array.count;
        int start = (int)args[1].as.number;
        int end = (argc >= 3) ? (int)args[2].as.number : len;
        if (start < 0) start += len;
        if (end < 0) end += len;
        if (start < 0) start = 0;
        if (end > len) end = len;
        Value arr = val_array(end - start > 0 ? end - start : 8);
        for (int i = start; i < end; i++) {
            val_array_push(&arr, val_copy(args[0].as.array.items[i]));
        }
        return arr;
    }
    return val_null();
}

/* map(arr, fn_name) -- handled specially in interpreter */
static Value bi_map(Value *args, int argc) {
    (void)args; (void)argc;
    /* This is handled in the interpreter for user-defined fn calls */
    return val_null();
}

/* filter(arr, fn_name) -- handled specially in interpreter */
static Value bi_filter(Value *args, int argc) {
    (void)args; (void)argc;
    return val_null();
}

/* reduce(arr, fn_name, init) -- handled specially in interpreter */
static Value bi_reduce(Value *args, int argc) {
    (void)args; (void)argc;
    return val_null();
}

/* ---- Math builtins ---- */

static Value bi_abs(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_NUMBER) return val_number(0);
    return val_number(fabs(args[0].as.number));
}

static Value bi_floor(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_NUMBER) return val_number(0);
    return val_number(floor(args[0].as.number));
}

static Value bi_ceil(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_NUMBER) return val_number(0);
    return val_number(ceil(args[0].as.number));
}

static Value bi_round(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_NUMBER) return val_number(0);
    return val_number(round(args[0].as.number));
}

static Value bi_min(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_NUMBER || args[1].type != VAL_NUMBER) return val_number(0);
    return val_number(args[0].as.number < args[1].as.number ? args[0].as.number : args[1].as.number);
}

static Value bi_max(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_NUMBER || args[1].type != VAL_NUMBER) return val_number(0);
    return val_number(args[0].as.number > args[1].as.number ? args[0].as.number : args[1].as.number);
}

static Value bi_pow(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_NUMBER || args[1].type != VAL_NUMBER) return val_number(0);
    return val_number(pow(args[0].as.number, args[1].as.number));
}

static Value bi_sqrt(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_NUMBER) return val_number(0);
    return val_number(sqrt(args[0].as.number));
}

/* type(x) - returns type name */
static Value bi_type(Value *args, int argc) {
    if (argc < 1) return val_string("null", 4);
    const char *tn = val_type_name(args[0]);
    return val_string(tn, (int)strlen(tn));
}

/* ---- String methods (called as __method_NAME(self, ...)) ---- */

static Value bi_method_upper(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_string("", 0);
    int len = args[0].as.string.len;
    char *s = malloc((size_t)len + 1);
    for (int i = 0; i < len; i++) s[i] = (char)toupper((unsigned char)args[0].as.string.str[i]);
    s[len] = '\0';
    return val_string_take(s, len);
}

static Value bi_method_lower(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_string("", 0);
    int len = args[0].as.string.len;
    char *s = malloc((size_t)len + 1);
    for (int i = 0; i < len; i++) s[i] = (char)tolower((unsigned char)args[0].as.string.str[i]);
    s[len] = '\0';
    return val_string_take(s, len);
}

static Value bi_method_trim(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_string("", 0);
    const char *s = args[0].as.string.str;
    int len = args[0].as.string.len;
    int start = 0, end = len;
    while (start < end && isspace((unsigned char)s[start])) start++;
    while (end > start && isspace((unsigned char)s[end - 1])) end--;
    return val_string(s + start, end - start);
}

static Value bi_method_contains(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_bool(0);
    return val_bool(strstr(args[0].as.string.str, args[1].as.string.str) != NULL);
}

static Value bi_method_replace(Value *args, int argc) {
    if (argc < 3 || args[0].type != VAL_STRING ||
        args[1].type != VAL_STRING || args[2].type != VAL_STRING)
        return (argc >= 1) ? val_copy(args[0]) : val_string("", 0);

    const char *src = args[0].as.string.str;
    const char *old = args[1].as.string.str;
    const char *rep = args[2].as.string.str;
    int oldlen = (int)strlen(old);
    int replen = (int)strlen(rep);

    if (oldlen == 0) return val_copy(args[0]);

    int cap = 256, len = 0;
    char *buf = malloc((size_t)cap);
    const char *p = src;
    while (*p) {
        const char *found = strstr(p, old);
        if (!found) {
            int rem = (int)strlen(p);
            while (len + rem + 1 >= cap) { cap *= 2; buf = realloc(buf, (size_t)cap); }
            memcpy(buf + len, p, (size_t)rem);
            len += rem;
            break;
        }
        int chunk = (int)(found - p);
        while (len + chunk + replen + 1 >= cap) { cap *= 2; buf = realloc(buf, (size_t)cap); }
        memcpy(buf + len, p, (size_t)chunk);
        len += chunk;
        memcpy(buf + len, rep, (size_t)replen);
        len += replen;
        p = found + oldlen;
    }
    buf[len] = '\0';
    return val_string_take(buf, len);
}

static Value bi_method_indexOf(Value *args, int argc) {
    if (argc < 2) return val_number(-1);

    /* String indexOf */
    if (args[0].type == VAL_STRING && args[1].type == VAL_STRING) {
        const char *found = strstr(args[0].as.string.str, args[1].as.string.str);
        if (!found) return val_number(-1);
        return val_number((double)(found - args[0].as.string.str));
    }

    /* Array indexOf */
    if (args[0].type == VAL_ARRAY) {
        for (int i = 0; i < args[0].as.array.count; i++) {
            if (val_equal(args[0].as.array.items[i], args[1])) {
                return val_number(i);
            }
        }
        return val_number(-1);
    }

    return val_number(-1);
}

/* ---- Array methods ---- */

static Value bi_method_includes(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_ARRAY) return val_bool(0);
    for (int i = 0; i < args[0].as.array.count; i++) {
        if (val_equal(args[0].as.array.items[i], args[1])) return val_bool(1);
    }
    return val_bool(0);
}

static Value bi_method_flat(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_ARRAY) return val_array(8);
    Value result = val_array(args[0].as.array.count * 2);
    for (int i = 0; i < args[0].as.array.count; i++) {
        Value item = args[0].as.array.items[i];
        if (item.type == VAL_ARRAY) {
            for (int j = 0; j < item.as.array.count; j++) {
                val_array_push(&result, val_copy(item.as.array.items[j]));
            }
        } else {
            val_array_push(&result, val_copy(item));
        }
    }
    return result;
}

static Value bi_method_concat(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_ARRAY || args[1].type != VAL_ARRAY)
        return (argc >= 1 && args[0].type == VAL_ARRAY) ? val_copy(args[0]) : val_array(8);
    Value result = val_array(args[0].as.array.count + args[1].as.array.count);
    for (int i = 0; i < args[0].as.array.count; i++) {
        val_array_push(&result, val_copy(args[0].as.array.items[i]));
    }
    for (int i = 0; i < args[1].as.array.count; i++) {
        val_array_push(&result, val_copy(args[1].as.array.items[i]));
    }
    return result;
}

/* Array .push() / .pop() / .length() as methods */
static Value bi_method_push(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_ARRAY) return val_null();
    val_array_push(&args[0], val_copy(args[1]));
    return val_null();
}

static Value bi_method_pop(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_ARRAY) return val_null();
    return val_array_pop(&args[0]);
}

static Value bi_method_length(Value *args, int argc) {
    if (argc < 1) return val_number(0);
    if (args[0].type == VAL_STRING) return val_number(args[0].as.string.len);
    if (args[0].type == VAL_ARRAY) return val_number(args[0].as.array.count);
    return val_number(0);
}

/* ---- Object methods ---- */

static Value bi_method_keys(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_OBJECT) return val_array(8);
    return table_keys(args[0].as.object);
}

static Value bi_method_values(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_OBJECT) return val_array(8);
    return table_values(args[0].as.object);
}

static Value bi_method_has(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_OBJECT || args[1].type != VAL_STRING)
        return val_bool(0);
    return val_bool(table_has(args[0].as.object, args[1].as.string.str));
}

/* ---- File I/O builtins ---- */

static Value bi_readFile(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_null();
    FILE *f = fopen(args[0].as.string.str, "r");
    if (!f) return val_null();
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    size_t nr = fread(buf, 1, (size_t)size, f);
    buf[nr] = '\0';
    fclose(f);
    return val_string_take(buf, (int)nr);
}

static Value bi_writeFile(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_bool(0);
    FILE *f = fopen(args[0].as.string.str, "w");
    if (!f) return val_bool(0);
    fwrite(args[1].as.string.str, 1, (size_t)args[1].as.string.len, f);
    fclose(f);
    return val_bool(1);
}

static Value bi_appendFile(Value *args, int argc) {
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_bool(0);
    FILE *f = fopen(args[0].as.string.str, "a");
    if (!f) return val_bool(0);
    fwrite(args[1].as.string.str, 1, (size_t)args[1].as.string.len, f);
    fclose(f);
    return val_bool(1);
}

/* ---- HTTP stubs ---- */

static Value bi_httpGet(Value *args, int argc) {
    (void)args; (void)argc;
    fprintf(stderr, "http not available in C build\n");
    return val_null();
}

static Value bi_httpPost(Value *args, int argc) {
    (void)args; (void)argc;
    fprintf(stderr, "http not available in C build\n");
    return val_null();
}

/* ---- JSON builtins ---- */

static Value bi_jsonParse(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_null();
    /* Simple JSON parse -- reuse the value_from_json approach */
    /* For now, return the string as-is. A full JSON parser would go here. */
    (void)args;
    return val_null();
}

static Value bi_jsonStringify(Value *args, int argc) {
    if (argc < 1) return val_string("null", 4);
    char *s = val_to_string(args[0]);
    int len = (int)strlen(s);
    return val_string_take(s, len);
}

/* ---- time ---- */

static Value bi_time(Value *args, int argc) {
    (void)args; (void)argc;
    return val_number((double)time(NULL));
}

static Value bi_clock(Value *args, int argc) {
    (void)args; (void)argc;
    return val_number((double)clock() / CLOCKS_PER_SEC);
}

/* ---- exit ---- */

static Value bi_exit(Value *args, int argc) {
    int code = 0;
    if (argc > 0 && args[0].type == VAL_NUMBER) code = (int)args[0].as.number;
    exit(code);
    return val_null(); /* unreachable */
}

/* ---- toString (alias for str) ---- */
static Value bi_toString(Value *args, int argc) {
    return bi_str(args, argc);
}

/* ---- number ---- */
static Value bi_number(Value *args, int argc) {
    if (argc < 1) return val_number(0);
    if (args[0].type == VAL_NUMBER) return args[0];
    if (args[0].type == VAL_STRING) {
        char *end;
        double val = strtod(args[0].as.string.str, &end);
        if (end == args[0].as.string.str) return val_number(0);
        return val_number(val);
    }
    if (args[0].type == VAL_BOOL) return val_number(args[0].as.boolean);
    return val_number(0);
}

/* ---- Sort/Reverse ---- */

static int value_compare(const void *a, const void *b) {
    const Value *va = (const Value *)a;
    const Value *vb = (const Value *)b;
    if (va->type == VAL_NUMBER && vb->type == VAL_NUMBER) {
        if (va->as.number < vb->as.number) return -1;
        if (va->as.number > vb->as.number) return 1;
        return 0;
    }
    if (va->type == VAL_STRING && vb->type == VAL_STRING) {
        return strcmp(va->as.string.str, vb->as.string.str);
    }
    return 0;
}

static Value bi_sort(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_ARRAY) return val_array(8);
    Value result = val_copy(args[0]);
    if (result.as.array.count > 1) {
        qsort(result.as.array.items, (size_t)result.as.array.count, sizeof(Value), value_compare);
    }
    return result;
}

static Value bi_reverse(Value *args, int argc) {
    if (argc < 1 || args[0].type != VAL_ARRAY) return val_array(8);
    Value result = val_array(args[0].as.array.count);
    for (int i = args[0].as.array.count - 1; i >= 0; i--) {
        val_array_push(&result, val_copy(args[0].as.array.items[i]));
    }
    return result;
}

/* stringify = same as str but with JSON-like array/object output */
static Value bi_stringify(Value *args, int argc) {
    if (argc < 1) return val_string("", 0);
    char *s = val_to_string(args[0]);
    int len = (int)strlen(s);
    return val_string_take(s, len);
}

/* ---- Register everything ---- */

void builtins_register(Interpreter *it) {
    /* Core */
    table_set(&it->builtins, "str", val_builtin(bi_str));
    table_set(&it->builtins, "toString", val_builtin(bi_toString));
    table_set(&it->builtins, "len", val_builtin(bi_len));
    table_set(&it->builtins, "push", val_builtin(bi_push));
    table_set(&it->builtins, "pop", val_builtin(bi_pop));
    table_set(&it->builtins, "range", val_builtin(bi_range));
    table_set(&it->builtins, "int", val_builtin(bi_int));
    table_set(&it->builtins, "float", val_builtin(bi_float));
    table_set(&it->builtins, "number", val_builtin(bi_number));
    table_set(&it->builtins, "input", val_builtin(bi_input));
    table_set(&it->builtins, "split", val_builtin(bi_split));
    table_set(&it->builtins, "join", val_builtin(bi_join));
    table_set(&it->builtins, "slice", val_builtin(bi_slice));
    table_set(&it->builtins, "keys", val_builtin(bi_keys));
    table_set(&it->builtins, "values", val_builtin(bi_values));
    table_set(&it->builtins, "has", val_builtin(bi_has));
    table_set(&it->builtins, "delete", val_builtin(bi_delete));

    /* Functional (stubs -- handled specially in interpreter) */
    table_set(&it->builtins, "map", val_builtin(bi_map));
    table_set(&it->builtins, "filter", val_builtin(bi_filter));
    table_set(&it->builtins, "reduce", val_builtin(bi_reduce));

    /* Math */
    table_set(&it->builtins, "abs", val_builtin(bi_abs));
    table_set(&it->builtins, "floor", val_builtin(bi_floor));
    table_set(&it->builtins, "ceil", val_builtin(bi_ceil));
    table_set(&it->builtins, "round", val_builtin(bi_round));
    table_set(&it->builtins, "min", val_builtin(bi_min));
    table_set(&it->builtins, "max", val_builtin(bi_max));
    table_set(&it->builtins, "pow", val_builtin(bi_pow));
    table_set(&it->builtins, "sqrt", val_builtin(bi_sqrt));

    /* Type */
    table_set(&it->builtins, "type", val_builtin(bi_type));

    /* String methods */
    table_set(&it->builtins, "__method_upper", val_builtin(bi_method_upper));
    table_set(&it->builtins, "__method_lower", val_builtin(bi_method_lower));
    table_set(&it->builtins, "__method_trim", val_builtin(bi_method_trim));
    table_set(&it->builtins, "__method_contains", val_builtin(bi_method_contains));
    table_set(&it->builtins, "__method_replace", val_builtin(bi_method_replace));
    table_set(&it->builtins, "__method_indexOf", val_builtin(bi_method_indexOf));

    /* Array methods */
    table_set(&it->builtins, "__method_includes", val_builtin(bi_method_includes));
    table_set(&it->builtins, "__method_flat", val_builtin(bi_method_flat));
    table_set(&it->builtins, "__method_concat", val_builtin(bi_method_concat));
    table_set(&it->builtins, "__method_push", val_builtin(bi_method_push));
    table_set(&it->builtins, "__method_pop", val_builtin(bi_method_pop));
    table_set(&it->builtins, "__method_length", val_builtin(bi_method_length));

    /* Object methods */
    table_set(&it->builtins, "__method_keys", val_builtin(bi_method_keys));
    table_set(&it->builtins, "__method_values", val_builtin(bi_method_values));
    table_set(&it->builtins, "__method_has", val_builtin(bi_method_has));

    /* File I/O */
    table_set(&it->builtins, "readFile", val_builtin(bi_readFile));
    table_set(&it->builtins, "writeFile", val_builtin(bi_writeFile));
    table_set(&it->builtins, "appendFile", val_builtin(bi_appendFile));

    /* HTTP stubs */
    table_set(&it->builtins, "httpGet", val_builtin(bi_httpGet));
    table_set(&it->builtins, "httpPost", val_builtin(bi_httpPost));

    /* JSON */
    table_set(&it->builtins, "jsonParse", val_builtin(bi_jsonParse));
    table_set(&it->builtins, "jsonStringify", val_builtin(bi_jsonStringify));

    /* Time */
    table_set(&it->builtins, "time", val_builtin(bi_time));
    table_set(&it->builtins, "clock", val_builtin(bi_clock));

    /* Sort/Reverse */
    table_set(&it->builtins, "sort", val_builtin(bi_sort));
    table_set(&it->builtins, "reverse", val_builtin(bi_reverse));
    table_set(&it->builtins, "stringify", val_builtin(bi_stringify));

    /* Exit */
    table_set(&it->builtins, "exit", val_builtin(bi_exit));
}
