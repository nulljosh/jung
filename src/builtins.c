#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

static ExecResult ok(Value *v) {
    ExecResult r = { STATUS_OK, v };
    return r;
}

static ExecResult throw_err(const char *msg) {
    ExecResult r = { STATUS_THROW, value_new_string(msg) };
    return r;
}

/* ---- builtin functions ---- */

static int bi_len(Value **args, int argc, ExecResult *res) {
    if (argc != 1) { *res = throw_err("len() takes 1 argument"); return 1; }
    Value *a = args[0];
    if (a->type == VAL_ARRAY)       { *res = ok(value_new_number(a->as.array.length)); return 1; }
    if (a->type == VAL_STRING)      { *res = ok(value_new_number(strlen(a->as.string))); return 1; }
    if (a->type == VAL_OBJECT)      { *res = ok(value_new_number(a->as.object.length)); return 1; }
    *res = throw_err("len() requires array, string, or object"); return 1;
}

static int bi_push(Value **args, int argc, ExecResult *res) {
    if (argc != 2) { *res = throw_err("push() takes 2 arguments"); return 1; }
    if (args[0]->type != VAL_ARRAY) { *res = throw_err("push() requires array"); return 1; }
    value_array_push(args[0], args[1]);
    value_retain(args[0]);
    *res = ok(args[0]);
    return 1;
}

static int bi_pop(Value **args, int argc, ExecResult *res) {
    if (argc != 1) { *res = throw_err("pop() takes 1 argument"); return 1; }
    if (args[0]->type != VAL_ARRAY || args[0]->as.array.length == 0) {
        *res = throw_err("pop() requires non-empty array"); return 1;
    }
    Value *item = value_array_pop(args[0]);
    /* item already has a ref from the array; we just hand it over */
    *res = ok(item);
    return 1;
}

static int bi_range(Value **args, int argc, ExecResult *res) {
    if (argc != 2) { *res = throw_err("range() takes 2 arguments"); return 1; }
    if (args[0]->type != VAL_NUMBER || args[1]->type != VAL_NUMBER) {
        *res = throw_err("range() requires numbers"); return 1;
    }
    int start = (int)args[0]->as.number;
    int end = (int)args[1]->as.number;
    Value *arr = value_new_array();
    for (int i = start; i < end; i++) {
        Value *n = value_new_number(i);
        value_array_push(arr, n);
        value_release(n);
    }
    *res = ok(arr);
    return 1;
}

static int bi_str(Value **args, int argc, ExecResult *res) {
    if (argc != 1) { *res = throw_err("str() takes 1 argument"); return 1; }
    char *s = value_to_string(args[0]);
    *res = ok(value_new_string_owned(s));
    return 1;
}

static int bi_int_conv(Value **args, int argc, ExecResult *res) {
    if (argc != 1) { *res = throw_err("int() takes 1 argument"); return 1; }
    Value *a = args[0];
    if (a->type == VAL_NUMBER) {
        *res = ok(value_new_number((double)(long long)a->as.number));
    } else if (a->type == VAL_STRING) {
        *res = ok(value_new_number(atof(a->as.string)));
    } else if (a->type == VAL_BOOL) {
        *res = ok(value_new_number(a->as.boolean ? 1.0 : 0.0));
    } else {
        *res = ok(value_new_number(0));
    }
    return 1;
}

static int bi_type(Value **args, int argc, ExecResult *res) {
    if (argc != 1) { *res = throw_err("type() takes 1 argument"); return 1; }
    const char *names[] = { "number", "string", "bool", "null", "array", "object", "function" };
    if (args[0]->type >= 0 && args[0]->type <= VAL_FUNCTION)
        *res = ok(value_new_string(names[args[0]->type]));
    else
        *res = ok(value_new_string("unknown"));
    return 1;
}

static int bi_slice(Value **args, int argc, ExecResult *res) {
    if (argc != 3) { *res = throw_err("slice() takes 3 arguments"); return 1; }
    if (args[0]->type != VAL_STRING) { *res = throw_err("slice() requires string"); return 1; }
    int slen = (int)strlen(args[0]->as.string);
    int start = (int)args[1]->as.number;
    int end = (int)args[2]->as.number;
    if (start < 0) start = 0;
    if (end > slen) end = slen;
    if (start >= end) { *res = ok(value_new_string("")); return 1; }
    int rlen = end - start;
    char *buf = malloc(rlen + 1);
    memcpy(buf, args[0]->as.string + start, rlen);
    buf[rlen] = '\0';
    *res = ok(value_new_string_owned(buf));
    return 1;
}

static int bi_split(Value **args, int argc, ExecResult *res) {
    if (argc != 2) { *res = throw_err("split() takes 2 arguments"); return 1; }
    if (args[0]->type != VAL_STRING || args[1]->type != VAL_STRING) {
        *res = throw_err("split() requires strings"); return 1;
    }
    Value *arr = value_new_array();
    const char *s = args[0]->as.string;
    const char *delim = args[1]->as.string;
    int dlen = (int)strlen(delim);

    if (dlen == 0) {
        /* Split into characters */
        for (int i = 0; s[i]; i++) {
            char c[2] = { s[i], '\0' };
            Value *v = value_new_string(c);
            value_array_push(arr, v);
            value_release(v);
        }
    } else {
        const char *p = s;
        while (1) {
            const char *found = strstr(p, delim);
            if (!found) {
                Value *v = value_new_string(p);
                value_array_push(arr, v);
                value_release(v);
                break;
            }
            int len = (int)(found - p);
            char *seg = malloc(len + 1);
            memcpy(seg, p, len);
            seg[len] = '\0';
            Value *v = value_new_string_owned(seg);
            value_array_push(arr, v);
            value_release(v);
            p = found + dlen;
        }
    }
    *res = ok(arr);
    return 1;
}

/* Comparison for sort */
static int value_compare(const void *a, const void *b) {
    Value *va = *(Value **)a;
    Value *vb = *(Value **)b;
    if (va->type == VAL_NUMBER && vb->type == VAL_NUMBER) {
        if (va->as.number < vb->as.number) return -1;
        if (va->as.number > vb->as.number) return 1;
        return 0;
    }
    if (va->type == VAL_STRING && vb->type == VAL_STRING) {
        return strcmp(va->as.string, vb->as.string);
    }
    return 0;
}

static int bi_sort(Value **args, int argc, ExecResult *res) {
    if (argc != 1 || args[0]->type != VAL_ARRAY) {
        *res = throw_err("sort() requires array"); return 1;
    }
    Value *src = args[0];
    Value *out = value_new_array();
    for (int i = 0; i < src->as.array.length; i++) {
        value_array_push(out, src->as.array.items[i]);
    }
    qsort(out->as.array.items, out->as.array.length, sizeof(Value *), value_compare);
    *res = ok(out);
    return 1;
}

static int bi_reverse(Value **args, int argc, ExecResult *res) {
    if (argc != 1 || args[0]->type != VAL_ARRAY) {
        *res = throw_err("reverse() requires array"); return 1;
    }
    Value *src = args[0];
    Value *out = value_new_array();
    for (int i = src->as.array.length - 1; i >= 0; i--) {
        value_array_push(out, src->as.array.items[i]);
    }
    *res = ok(out);
    return 1;
}

static int bi_join(Value **args, int argc, ExecResult *res) {
    if (argc < 1 || argc > 2 || args[0]->type != VAL_ARRAY) {
        *res = throw_err("join() requires array and optional delimiter"); return 1;
    }
    const char *delim = (argc == 2 && args[1]->type == VAL_STRING) ? args[1]->as.string : "";
    int dlen = (int)strlen(delim);
    Value *arr = args[0];

    int total = 0;
    char **strs = calloc(arr->as.array.length, sizeof(char *));
    for (int i = 0; i < arr->as.array.length; i++) {
        strs[i] = value_to_string(arr->as.array.items[i]);
        total += (int)strlen(strs[i]);
        if (i > 0) total += dlen;
    }
    char *buf = malloc(total + 1);
    buf[0] = '\0';
    int pos = 0;
    for (int i = 0; i < arr->as.array.length; i++) {
        if (i > 0) { memcpy(buf + pos, delim, dlen); pos += dlen; }
        int slen = (int)strlen(strs[i]);
        memcpy(buf + pos, strs[i], slen); pos += slen;
        free(strs[i]);
    }
    buf[pos] = '\0';
    free(strs);
    *res = ok(value_new_string_owned(buf));
    return 1;
}

/* Math builtins */
static int bi_abs(Value **args, int argc, ExecResult *res) {
    if (argc != 1 || args[0]->type != VAL_NUMBER) { *res = throw_err("abs() requires number"); return 1; }
    *res = ok(value_new_number(fabs(args[0]->as.number))); return 1;
}
static int bi_floor(Value **args, int argc, ExecResult *res) {
    if (argc != 1 || args[0]->type != VAL_NUMBER) { *res = throw_err("floor() requires number"); return 1; }
    *res = ok(value_new_number(floor(args[0]->as.number))); return 1;
}
static int bi_ceil(Value **args, int argc, ExecResult *res) {
    if (argc != 1 || args[0]->type != VAL_NUMBER) { *res = throw_err("ceil() requires number"); return 1; }
    *res = ok(value_new_number(ceil(args[0]->as.number))); return 1;
}
static int bi_round(Value **args, int argc, ExecResult *res) {
    if (argc != 1 || args[0]->type != VAL_NUMBER) { *res = throw_err("round() requires number"); return 1; }
    *res = ok(value_new_number(round(args[0]->as.number))); return 1;
}
static int bi_sqrt(Value **args, int argc, ExecResult *res) {
    if (argc != 1 || args[0]->type != VAL_NUMBER) { *res = throw_err("sqrt() requires number"); return 1; }
    *res = ok(value_new_number(sqrt(args[0]->as.number))); return 1;
}
static int bi_min(Value **args, int argc, ExecResult *res) {
    if (argc != 2 || args[0]->type != VAL_NUMBER || args[1]->type != VAL_NUMBER) {
        *res = throw_err("min() requires 2 numbers"); return 1;
    }
    *res = ok(value_new_number(fmin(args[0]->as.number, args[1]->as.number))); return 1;
}
static int bi_max(Value **args, int argc, ExecResult *res) {
    if (argc != 2 || args[0]->type != VAL_NUMBER || args[1]->type != VAL_NUMBER) {
        *res = throw_err("max() requires 2 numbers"); return 1;
    }
    *res = ok(value_new_number(fmax(args[0]->as.number, args[1]->as.number))); return 1;
}
static int bi_pow_fn(Value **args, int argc, ExecResult *res) {
    if (argc != 2 || args[0]->type != VAL_NUMBER || args[1]->type != VAL_NUMBER) {
        *res = throw_err("pow() requires 2 numbers"); return 1;
    }
    *res = ok(value_new_number(pow(args[0]->as.number, args[1]->as.number))); return 1;
}

/* File I/O */
static int bi_read(Value **args, int argc, ExecResult *res) {
    if (argc != 1 || args[0]->type != VAL_STRING) {
        *res = throw_err("read() requires string path"); return 1;
    }
    FILE *f = fopen(args[0]->as.string, "r");
    if (!f) { *res = throw_err("read(): file not found"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    *res = ok(value_new_string_owned(buf));
    return 1;
}

static int bi_write(Value **args, int argc, ExecResult *res) {
    if (argc != 2 || args[0]->type != VAL_STRING || args[1]->type != VAL_STRING) {
        *res = throw_err("write() requires (path, content)"); return 1;
    }
    FILE *f = fopen(args[0]->as.string, "w");
    if (!f) { *res = throw_err("write(): cannot open file"); return 1; }
    fputs(args[1]->as.string, f);
    fclose(f);
    value_retain(args[1]);
    *res = ok(args[1]);
    return 1;
}

static int bi_append(Value **args, int argc, ExecResult *res) {
    if (argc != 2 || args[0]->type != VAL_STRING || args[1]->type != VAL_STRING) {
        *res = throw_err("append() requires (path, content)"); return 1;
    }
    FILE *f = fopen(args[0]->as.string, "a");
    if (!f) { *res = throw_err("append(): cannot open file"); return 1; }
    fputs(args[1]->as.string, f);
    fclose(f);
    value_retain(args[1]);
    *res = ok(args[1]);
    return 1;
}

/* JSON */
static int bi_parse(Value **args, int argc, ExecResult *res) {
    if (argc != 1 || args[0]->type != VAL_STRING) {
        *res = throw_err("parse() requires string"); return 1;
    }
    int pos = 0;
    Value *v = value_from_json(args[0]->as.string, &pos);
    *res = ok(v);
    return 1;
}

static int bi_stringify(Value **args, int argc, ExecResult *res) {
    if (argc != 1) { *res = throw_err("stringify() takes 1 argument"); return 1; }
    char *s = value_to_json(args[0]);
    *res = ok(value_new_string_owned(s));
    return 1;
}

/* HTTP stubs */
static int bi_http_get(Value **args, int argc, ExecResult *res) {
    (void)args; (void)argc;
    *res = throw_err("http_get() not available in C build");
    return 1;
}

static int bi_http_post(Value **args, int argc, ExecResult *res) {
    (void)args; (void)argc;
    *res = throw_err("http_post() not available in C build");
    return 1;
}

/* map/filter/reduce -- these need the interpreter to call user functions */
static int bi_map(Interpreter *interp, Value **args, int argc, ExecResult *res);
static int bi_filter(Interpreter *interp, Value **args, int argc, ExecResult *res);
static int bi_reduce(Interpreter *interp, Value **args, int argc, ExecResult *res);

/* Helper: call a user-defined function stored in interp->functions */
static ExecResult call_user_fn(Interpreter *interp, Value *fn, Value **call_args, int call_argc) {
    if (fn->type != VAL_FUNCTION) {
        return throw_err("not a function");
    }

    /* Save scope */
    Table *saved = interp->variables;
    interp->variables = table_copy(saved);

    /* Bind parameters */
    for (int i = 0; i < fn->as.function.param_count && i < call_argc; i++) {
        table_set(interp->variables, fn->as.function.params[i], call_args[i]);
    }

    interp->call_depth++;
    ExecResult r = exec_stmts(interp, fn->as.function.body_stmts, fn->as.function.body_count);
    interp->call_depth--;

    if (r.status == STATUS_RETURN) r.status = STATUS_OK;

    /* Restore scope */
    table_free(interp->variables);
    interp->variables = saved;

    return r;
}

static int bi_map(Interpreter *interp, Value **args, int argc, ExecResult *res) {
    if (argc != 2) { *res = throw_err("map() takes 2 arguments"); return 1; }
    if (args[0]->type != VAL_STRING || args[1]->type != VAL_ARRAY) {
        *res = throw_err("map(func_name, array)"); return 1;
    }
    Value *fn = table_get(interp->functions, args[0]->as.string);
    if (!fn) { *res = throw_err("map(): function not found"); return 1; }

    Value *arr = args[1];
    Value *out = value_new_array();
    for (int i = 0; i < arr->as.array.length; i++) {
        Value *call_args[1] = { arr->as.array.items[i] };
        ExecResult r = call_user_fn(interp, fn, call_args, 1);
        if (r.status != STATUS_OK) { value_release(out); *res = r; return 1; }
        if (r.value) {
            value_array_push(out, r.value);
            value_release(r.value);
        } else {
            Value *null = value_new_null();
            value_array_push(out, null);
            value_release(null);
        }
    }
    *res = ok(out);
    return 1;
}

static int bi_filter(Interpreter *interp, Value **args, int argc, ExecResult *res) {
    if (argc != 2) { *res = throw_err("filter() takes 2 arguments"); return 1; }
    if (args[0]->type != VAL_STRING || args[1]->type != VAL_ARRAY) {
        *res = throw_err("filter(func_name, array)"); return 1;
    }
    Value *fn = table_get(interp->functions, args[0]->as.string);
    if (!fn) { *res = throw_err("filter(): function not found"); return 1; }

    Value *arr = args[1];
    Value *out = value_new_array();
    for (int i = 0; i < arr->as.array.length; i++) {
        Value *call_args[1] = { arr->as.array.items[i] };
        ExecResult r = call_user_fn(interp, fn, call_args, 1);
        if (r.status != STATUS_OK) { value_release(out); *res = r; return 1; }
        if (r.value && value_is_truthy(r.value)) {
            value_array_push(out, arr->as.array.items[i]);
        }
        if (r.value) value_release(r.value);
    }
    *res = ok(out);
    return 1;
}

static int bi_reduce(Interpreter *interp, Value **args, int argc, ExecResult *res) {
    if (argc < 2 || argc > 3) { *res = throw_err("reduce() takes 2-3 arguments"); return 1; }
    if (args[0]->type != VAL_STRING || args[1]->type != VAL_ARRAY) {
        *res = throw_err("reduce(func_name, array [, init])"); return 1;
    }
    Value *fn = table_get(interp->functions, args[0]->as.string);
    if (!fn) { *res = throw_err("reduce(): function not found"); return 1; }

    Value *arr = args[1];
    if (arr->as.array.length == 0) {
        if (argc == 3) { value_retain(args[2]); *res = ok(args[2]); }
        else { *res = ok(value_new_null()); }
        return 1;
    }

    Value *acc;
    int start;
    if (argc == 3) {
        acc = args[2];
        value_retain(acc);
        start = 0;
    } else {
        acc = arr->as.array.items[0];
        value_retain(acc);
        start = 1;
    }

    for (int i = start; i < arr->as.array.length; i++) {
        Value *call_args[2] = { acc, arr->as.array.items[i] };
        ExecResult r = call_user_fn(interp, fn, call_args, 2);
        value_release(acc);
        if (r.status != STATUS_OK) { *res = r; return 1; }
        acc = r.value ? r.value : value_new_null();
    }
    *res = ok(acc);
    return 1;
}

/* ---- dispatch table ---- */

int builtin_call(Interpreter *interp, const char *name,
                 Value **args, int arg_count, ExecResult *result) {
    /* map/filter/reduce need the interpreter */
    if (strcmp(name, "map") == 0)    return bi_map(interp, args, arg_count, result);
    if (strcmp(name, "filter") == 0) return bi_filter(interp, args, arg_count, result);
    if (strcmp(name, "reduce") == 0) return bi_reduce(interp, args, arg_count, result);

    /* Simple builtins */
    if (strcmp(name, "len") == 0)       return bi_len(args, arg_count, result);
    if (strcmp(name, "push") == 0)      return bi_push(args, arg_count, result);
    if (strcmp(name, "pop") == 0)       return bi_pop(args, arg_count, result);
    if (strcmp(name, "range") == 0)     return bi_range(args, arg_count, result);
    if (strcmp(name, "str") == 0)       return bi_str(args, arg_count, result);
    if (strcmp(name, "int") == 0)       return bi_int_conv(args, arg_count, result);
    if (strcmp(name, "type") == 0)      return bi_type(args, arg_count, result);
    if (strcmp(name, "slice") == 0)     return bi_slice(args, arg_count, result);
    if (strcmp(name, "split") == 0)     return bi_split(args, arg_count, result);
    if (strcmp(name, "sort") == 0)      return bi_sort(args, arg_count, result);
    if (strcmp(name, "reverse") == 0)   return bi_reverse(args, arg_count, result);
    if (strcmp(name, "join") == 0)      return bi_join(args, arg_count, result);
    if (strcmp(name, "abs") == 0)       return bi_abs(args, arg_count, result);
    if (strcmp(name, "floor") == 0)     return bi_floor(args, arg_count, result);
    if (strcmp(name, "ceil") == 0)      return bi_ceil(args, arg_count, result);
    if (strcmp(name, "round") == 0)     return bi_round(args, arg_count, result);
    if (strcmp(name, "sqrt") == 0)      return bi_sqrt(args, arg_count, result);
    if (strcmp(name, "min") == 0)       return bi_min(args, arg_count, result);
    if (strcmp(name, "max") == 0)       return bi_max(args, arg_count, result);
    if (strcmp(name, "pow") == 0)       return bi_pow_fn(args, arg_count, result);
    if (strcmp(name, "read") == 0)      return bi_read(args, arg_count, result);
    if (strcmp(name, "write") == 0)     return bi_write(args, arg_count, result);
    if (strcmp(name, "append") == 0)    return bi_append(args, arg_count, result);
    if (strcmp(name, "parse") == 0)     return bi_parse(args, arg_count, result);
    if (strcmp(name, "stringify") == 0) return bi_stringify(args, arg_count, result);
    if (strcmp(name, "http_get") == 0)  return bi_http_get(args, arg_count, result);
    if (strcmp(name, "http_post") == 0) return bi_http_post(args, arg_count, result);

    return 0; /* not a builtin */
}

/* ---- method dispatch ---- */

int builtin_method(Interpreter *interp, Value *obj, const char *method,
                   Value **args, int arg_count, ExecResult *result) {
    (void)interp;

    /* Object methods */
    if (obj->type == VAL_OBJECT) {
        if (strcmp(method, "keys") == 0) {
            Value *arr = value_new_array();
            for (int i = 0; i < obj->as.object.length; i++) {
                Value *k = value_new_string(obj->as.object.keys[i]);
                value_array_push(arr, k);
                value_release(k);
            }
            *result = ok(arr);
            return 1;
        }
        if (strcmp(method, "values") == 0) {
            Value *arr = value_new_array();
            for (int i = 0; i < obj->as.object.length; i++) {
                value_array_push(arr, obj->as.object.values[i]);
            }
            *result = ok(arr);
            return 1;
        }
        if (strcmp(method, "has") == 0) {
            if (arg_count != 1 || args[0]->type != VAL_STRING) {
                *result = throw_err("has() requires string key"); return 1;
            }
            *result = ok(value_new_bool(value_object_has(obj, args[0]->as.string)));
            return 1;
        }
    }

    /* String methods */
    if (obj->type == VAL_STRING) {
        if (strcmp(method, "upper") == 0) {
            char *s = strdup(obj->as.string);
            for (int i = 0; s[i]; i++) s[i] = (char)toupper((unsigned char)s[i]);
            *result = ok(value_new_string_owned(s));
            return 1;
        }
        if (strcmp(method, "lower") == 0) {
            char *s = strdup(obj->as.string);
            for (int i = 0; s[i]; i++) s[i] = (char)tolower((unsigned char)s[i]);
            *result = ok(value_new_string_owned(s));
            return 1;
        }
        if (strcmp(method, "trim") == 0) {
            const char *start = obj->as.string;
            while (*start && isspace((unsigned char)*start)) start++;
            const char *end = obj->as.string + strlen(obj->as.string) - 1;
            while (end > start && isspace((unsigned char)*end)) end--;
            int len = (int)(end - start + 1);
            char *s = malloc(len + 1);
            memcpy(s, start, len);
            s[len] = '\0';
            *result = ok(value_new_string_owned(s));
            return 1;
        }
        if (strcmp(method, "contains") == 0) {
            if (arg_count != 1 || args[0]->type != VAL_STRING) {
                *result = throw_err("contains() requires string"); return 1;
            }
            *result = ok(value_new_bool(strstr(obj->as.string, args[0]->as.string) != NULL));
            return 1;
        }
        if (strcmp(method, "replace") == 0) {
            if (arg_count != 2 || args[0]->type != VAL_STRING || args[1]->type != VAL_STRING) {
                *result = throw_err("replace() requires (old, new)"); return 1;
            }
            const char *src = obj->as.string;
            const char *old = args[0]->as.string;
            const char *rep = args[1]->as.string;
            int olen = (int)strlen(old);
            int rlen = (int)strlen(rep);
            if (olen == 0) { value_retain(obj); *result = ok(obj); return 1; }

            /* Count occurrences */
            int count = 0;
            const char *p = src;
            while ((p = strstr(p, old))) { count++; p += olen; }

            int newlen = (int)strlen(src) + count * (rlen - olen);
            char *buf = malloc(newlen + 1);
            char *out = buf;
            p = src;
            while (1) {
                const char *found = strstr(p, old);
                if (!found) { strcpy(out, p); break; }
                int seg = (int)(found - p);
                memcpy(out, p, seg);
                out += seg;
                memcpy(out, rep, rlen);
                out += rlen;
                p = found + olen;
            }
            *result = ok(value_new_string_owned(buf));
            return 1;
        }
        if (strcmp(method, "indexOf") == 0) {
            if (arg_count != 1 || args[0]->type != VAL_STRING) {
                *result = throw_err("indexOf() requires string"); return 1;
            }
            const char *found = strstr(obj->as.string, args[0]->as.string);
            if (found) *result = ok(value_new_number((double)(found - obj->as.string)));
            else *result = ok(value_new_number(-1));
            return 1;
        }
        if (strcmp(method, "split") == 0) {
            Value *bi_args[2] = { obj, arg_count > 0 ? args[0] : NULL };
            int bc = arg_count > 0 ? 2 : 1;
            if (bc == 1) {
                /* Split with no delimiter = split each char */
                bi_args[1] = value_new_string("");
                bc = 2;
                int r = bi_split(bi_args, bc, result);
                value_release(bi_args[1]);
                return r;
            }
            return bi_split(bi_args, bc, result);
        }
    }

    /* Array methods */
    if (obj->type == VAL_ARRAY) {
        if (strcmp(method, "includes") == 0) {
            if (arg_count != 1) { *result = throw_err("includes() takes 1 argument"); return 1; }
            int found = 0;
            for (int i = 0; i < obj->as.array.length; i++) {
                if (value_equals(obj->as.array.items[i], args[0])) { found = 1; break; }
            }
            *result = ok(value_new_bool(found));
            return 1;
        }
        if (strcmp(method, "flat") == 0) {
            Value *out = value_new_array();
            for (int i = 0; i < obj->as.array.length; i++) {
                Value *item = obj->as.array.items[i];
                if (item->type == VAL_ARRAY) {
                    for (int j = 0; j < item->as.array.length; j++) {
                        value_array_push(out, item->as.array.items[j]);
                    }
                } else {
                    value_array_push(out, item);
                }
            }
            *result = ok(out);
            return 1;
        }
        if (strcmp(method, "concat") == 0) {
            if (arg_count != 1 || args[0]->type != VAL_ARRAY) {
                *result = throw_err("concat() requires array"); return 1;
            }
            Value *out = value_new_array();
            for (int i = 0; i < obj->as.array.length; i++)
                value_array_push(out, obj->as.array.items[i]);
            for (int i = 0; i < args[0]->as.array.length; i++)
                value_array_push(out, args[0]->as.array.items[i]);
            *result = ok(out);
            return 1;
        }
        if (strcmp(method, "push") == 0) {
            if (arg_count != 1) { *result = throw_err("push() takes 1 argument"); return 1; }
            value_array_push(obj, args[0]);
            value_retain(obj);
            *result = ok(obj);
            return 1;
        }
        if (strcmp(method, "pop") == 0) {
            if (obj->as.array.length == 0) { *result = throw_err("pop() on empty array"); return 1; }
            Value *item = value_array_pop(obj);
            *result = ok(item);
            return 1;
        }
    }

    return 0; /* not a builtin method */
}
