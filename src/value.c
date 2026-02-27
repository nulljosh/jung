#include "value.h"
#include "table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

Value val_null(void) {
    Value v;
    v.type = VAL_NULL;
    v.refcount = 0;
    return v;
}

Value val_bool(int b) {
    Value v;
    v.type = VAL_BOOL;
    v.refcount = 0;
    v.as.boolean = b ? 1 : 0;
    return v;
}

Value val_number(double n) {
    Value v;
    v.type = VAL_NUMBER;
    v.refcount = 0;
    v.as.number = n;
    return v;
}

Value val_string(const char *s, int len) {
    Value v;
    v.type = VAL_STRING;
    v.refcount = 0;
    v.as.string.str = malloc((size_t)len + 1);
    memcpy(v.as.string.str, s, (size_t)len);
    v.as.string.str[len] = '\0';
    v.as.string.len = len;
    return v;
}

Value val_string_take(char *s, int len) {
    Value v;
    v.type = VAL_STRING;
    v.refcount = 0;
    v.as.string.str = s;
    v.as.string.len = len;
    return v;
}

Value val_array(int initial_cap) {
    Value v;
    v.type = VAL_ARRAY;
    v.refcount = 0;
    if (initial_cap < 8) initial_cap = 8;
    v.as.array.items = malloc(sizeof(Value) * (size_t)initial_cap);
    v.as.array.count = 0;
    v.as.array.cap = initial_cap;
    return v;
}

Value val_object(void) {
    Value v;
    v.type = VAL_OBJECT;
    v.refcount = 0;
    v.as.object = malloc(sizeof(Table));
    table_init(v.as.object);
    return v;
}

Value val_func(FuncDef *f) {
    Value v;
    v.type = VAL_FUNCTION;
    v.refcount = 0;
    v.as.func = f;
    return v;
}

Value val_builtin(BuiltinFn fn) {
    Value v;
    v.type = VAL_BUILTIN;
    v.refcount = 0;
    v.as.builtin = fn;
    return v;
}

Value val_copy(Value v) {
    /* For simple types, just copy. For heap types, dup the data. */
    Value out = v;
    if (v.type == VAL_STRING) {
        out.as.string.str = malloc((size_t)v.as.string.len + 1);
        memcpy(out.as.string.str, v.as.string.str, (size_t)v.as.string.len + 1);
    } else if (v.type == VAL_ARRAY) {
        int cap = v.as.array.cap;
        int cnt = v.as.array.count;
        out.as.array.items = malloc(sizeof(Value) * (size_t)cap);
        out.as.array.cap = cap;
        out.as.array.count = cnt;
        for (int i = 0; i < cnt; i++) {
            out.as.array.items[i] = val_copy(v.as.array.items[i]);
        }
    } else if (v.type == VAL_OBJECT) {
        /* Shallow copy: share the Table pointer for reference semantics */
        out.as.object = v.as.object;
        if (v.as.object) v.as.object->refcount++;
    }
    return out;
}

void val_free(Value *v) {
    if (v->type == VAL_STRING) {
        free(v->as.string.str);
        v->as.string.str = NULL;
    } else if (v->type == VAL_ARRAY) {
        for (int i = 0; i < v->as.array.count; i++) {
            val_free(&v->as.array.items[i]);
        }
        free(v->as.array.items);
        v->as.array.items = NULL;
        v->as.array.count = 0;
    } else if (v->type == VAL_OBJECT) {
        if (v->as.object) {
            v->as.object->refcount--;
            if (v->as.object->refcount <= 0) {
                table_free(v->as.object);
                free(v->as.object);
            }
            v->as.object = NULL;
        }
    }
    v->type = VAL_NULL;
}

void val_array_push(Value *arr, Value item) {
    if (arr->type != VAL_ARRAY) return;
    if (arr->as.array.count >= arr->as.array.cap) {
        arr->as.array.cap *= 2;
        arr->as.array.items = realloc(arr->as.array.items, sizeof(Value) * (size_t)arr->as.array.cap);
    }
    arr->as.array.items[arr->as.array.count++] = item;
}

Value val_array_pop(Value *arr) {
    if (arr->type != VAL_ARRAY || arr->as.array.count == 0) return val_null();
    return arr->as.array.items[--arr->as.array.count];
}

Value val_array_get(Value *arr, int idx) {
    if (arr->type != VAL_ARRAY) return val_null();
    if (idx < 0 || idx >= arr->as.array.count) return val_null();
    return arr->as.array.items[idx];
}

void val_array_set(Value *arr, int idx, Value item) {
    if (arr->type != VAL_ARRAY) return;
    if (idx < 0 || idx >= arr->as.array.count) return;
    val_free(&arr->as.array.items[idx]);
    arr->as.array.items[idx] = item;
}

int val_is_truthy(Value v) {
    switch (v.type) {
        case VAL_NULL: return 0;
        case VAL_BOOL: return v.as.boolean;
        case VAL_NUMBER: return v.as.number != 0;
        case VAL_STRING: return v.as.string.len > 0;
        case VAL_ARRAY: return v.as.array.count > 0;
        case VAL_OBJECT: return 1;
        case VAL_FUNCTION: return 1;
        case VAL_BUILTIN: return 1;
    }
    return 0;
}

int val_equal(Value a, Value b) {
    if (a.type != b.type) return 0;
    switch (a.type) {
        case VAL_NULL: return 1;
        case VAL_BOOL: return a.as.boolean == b.as.boolean;
        case VAL_NUMBER: return a.as.number == b.as.number;
        case VAL_STRING:
            if (a.as.string.len != b.as.string.len) return 0;
            return strcmp(a.as.string.str, b.as.string.str) == 0;
        default: return 0;
    }
}

char *val_to_string(Value v) {
    char buf[256];
    switch (v.type) {
        case VAL_NULL:
            return strdup("null");
        case VAL_BOOL:
            return strdup(v.as.boolean ? "true" : "false");
        case VAL_NUMBER: {
            double n = v.as.number;
            if (n == floor(n) && n >= -1e15 && n <= 1e15) {
                snprintf(buf, sizeof(buf), "%ld", (long)n);
            } else {
                snprintf(buf, sizeof(buf), "%g", n);
            }
            return strdup(buf);
        }
        case VAL_STRING:
            return strdup(v.as.string.str);
        case VAL_ARRAY: {
            /* Build string like [1, 2, 3] */
            int cap = 256;
            char *out = malloc((size_t)cap);
            int len = 0;
            out[len++] = '[';
            for (int i = 0; i < v.as.array.count; i++) {
                if (i > 0) { out[len++] = ','; out[len++] = ' '; }
                char *s = val_to_string(v.as.array.items[i]);
                int slen = (int)strlen(s);
                while (len + slen + 4 >= cap) { cap *= 2; out = realloc(out, (size_t)cap); }
                /* Quote strings in array display */
                if (v.as.array.items[i].type == VAL_STRING) {
                    out[len++] = '"';
                    memcpy(out + len, s, (size_t)slen);
                    len += slen;
                    out[len++] = '"';
                } else {
                    memcpy(out + len, s, (size_t)slen);
                    len += slen;
                }
                free(s);
            }
            if (len + 2 >= cap) { cap += 4; out = realloc(out, (size_t)cap); }
            out[len++] = ']';
            out[len] = '\0';
            return out;
        }
        case VAL_OBJECT: {
            int cap = 256;
            char *out = malloc((size_t)cap);
            int len = 0;
            out[len++] = '{';
            int first = 1;
            for (int i = 0; i < v.as.object->cap; i++) {
                TableEntry *e = v.as.object->entries[i];
                while (e) {
                    if (!first) { out[len++] = ','; out[len++] = ' '; }
                    first = 0;
                    /* skip internal __class__ etc */
                    int klen = (int)strlen(e->key);
                    char *vs = val_to_string(e->value);
                    int vlen = (int)strlen(vs);
                    while (len + klen + vlen + 10 >= cap) { cap *= 2; out = realloc(out, (size_t)cap); }
                    memcpy(out + len, e->key, (size_t)klen);
                    len += klen;
                    out[len++] = ':';
                    out[len++] = ' ';
                    if (e->value.type == VAL_STRING) {
                        out[len++] = '"';
                        memcpy(out + len, vs, (size_t)vlen);
                        len += vlen;
                        out[len++] = '"';
                    } else {
                        memcpy(out + len, vs, (size_t)vlen);
                        len += vlen;
                    }
                    free(vs);
                    e = e->next;
                }
            }
            if (len + 2 >= cap) { cap += 4; out = realloc(out, (size_t)cap); }
            out[len++] = '}';
            out[len] = '\0';
            return out;
        }
        case VAL_FUNCTION:
            snprintf(buf, sizeof(buf), "<fn %s>", v.as.func ? v.as.func->name : "?");
            return strdup(buf);
        case VAL_BUILTIN:
            return strdup("<builtin>");
    }
    return strdup("null");
}

const char *val_type_name(Value v) {
    switch (v.type) {
        case VAL_NULL: return "null";
        case VAL_BOOL: return "bool";
        case VAL_NUMBER: return "number";
        case VAL_STRING: return "string";
        case VAL_ARRAY: return "array";
        case VAL_OBJECT: return "object";
        case VAL_FUNCTION: return "function";
        case VAL_BUILTIN: return "function";
    }
    return "unknown";
}
