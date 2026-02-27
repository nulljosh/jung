#include "value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

Value *value_new_number(double n) {
    Value *v = calloc(1, sizeof(Value));
    v->type = VAL_NUMBER;
    v->refcount = 1;
    v->as.number = n;
    return v;
}

Value *value_new_string(const char *s) {
    Value *v = calloc(1, sizeof(Value));
    v->type = VAL_STRING;
    v->refcount = 1;
    v->as.string = strdup(s ? s : "");
    return v;
}

Value *value_new_string_owned(char *s) {
    Value *v = calloc(1, sizeof(Value));
    v->type = VAL_STRING;
    v->refcount = 1;
    v->as.string = s;
    return v;
}

Value *value_new_bool(int b) {
    Value *v = calloc(1, sizeof(Value));
    v->type = VAL_BOOL;
    v->refcount = 1;
    v->as.boolean = b ? 1 : 0;
    return v;
}

Value *value_new_null(void) {
    Value *v = calloc(1, sizeof(Value));
    v->type = VAL_NULL;
    v->refcount = 1;
    return v;
}

Value *value_new_array(void) {
    Value *v = calloc(1, sizeof(Value));
    v->type = VAL_ARRAY;
    v->refcount = 1;
    v->as.array.capacity = 8;
    v->as.array.length = 0;
    v->as.array.items = calloc(8, sizeof(Value *));
    return v;
}

Value *value_new_object(void) {
    Value *v = calloc(1, sizeof(Value));
    v->type = VAL_OBJECT;
    v->refcount = 1;
    v->as.object.capacity = 8;
    v->as.object.length = 0;
    v->as.object.keys = calloc(8, sizeof(char *));
    v->as.object.values = calloc(8, sizeof(Value *));
    return v;
}

Value *value_new_function(const char *name, char **params, int param_count,
                          struct ASTNode **body_stmts, int body_count) {
    Value *v = calloc(1, sizeof(Value));
    v->type = VAL_FUNCTION;
    v->refcount = 1;
    v->as.function.name = name ? strdup(name) : NULL;
    v->as.function.params = NULL;
    v->as.function.param_count = param_count;
    if (param_count > 0) {
        v->as.function.params = calloc(param_count, sizeof(char *));
        for (int i = 0; i < param_count; i++) {
            v->as.function.params[i] = strdup(params[i]);
        }
    }
    v->as.function.body_stmts = body_stmts;
    v->as.function.body_count = body_count;
    return v;
}

void value_retain(Value *v) {
    if (v) v->refcount++;
}

void value_release(Value *v) {
    if (!v) return;
    v->refcount--;
    if (v->refcount > 0) return;

    switch (v->type) {
    case VAL_STRING:
        free(v->as.string);
        break;
    case VAL_ARRAY:
        for (int i = 0; i < v->as.array.length; i++) {
            value_release(v->as.array.items[i]);
        }
        free(v->as.array.items);
        break;
    case VAL_OBJECT:
        for (int i = 0; i < v->as.object.length; i++) {
            free(v->as.object.keys[i]);
            value_release(v->as.object.values[i]);
        }
        free(v->as.object.keys);
        free(v->as.object.values);
        break;
    case VAL_FUNCTION:
        free(v->as.function.name);
        for (int i = 0; i < v->as.function.param_count; i++) {
            free(v->as.function.params[i]);
        }
        free(v->as.function.params);
        /* body_stmts not owned by function value -- owned by AST */
        break;
    default:
        break;
    }
    free(v);
}

/* Format a double: strip trailing zeros, show integers without decimal */
static char *format_number(double n) {
    char buf[64];
    if (n == (long long)n && fabs(n) < 1e15) {
        snprintf(buf, sizeof(buf), "%lld", (long long)n);
    } else {
        snprintf(buf, sizeof(buf), "%.14g", n);
    }
    return strdup(buf);
}

char *value_to_string(Value *v) {
    if (!v) return strdup("null");

    switch (v->type) {
    case VAL_NUMBER:
        return format_number(v->as.number);
    case VAL_STRING:
        return strdup(v->as.string);
    case VAL_BOOL:
        return strdup(v->as.boolean ? "true" : "false");
    case VAL_NULL:
        return strdup("null");
    case VAL_ARRAY: {
        /* Use JSON-like format */
        char *json = value_to_json(v);
        return json;
    }
    case VAL_OBJECT: {
        char *json = value_to_json(v);
        return json;
    }
    case VAL_FUNCTION: {
        char buf[128];
        snprintf(buf, sizeof(buf), "<fn %s>",
                 v->as.function.name ? v->as.function.name : "anonymous");
        return strdup(buf);
    }
    }
    return strdup("?");
}

int value_is_truthy(Value *v) {
    if (!v) return 0;
    switch (v->type) {
    case VAL_NULL:   return 0;
    case VAL_BOOL:   return v->as.boolean;
    case VAL_NUMBER: return v->as.number != 0.0;
    case VAL_STRING: return v->as.string[0] != '\0';
    case VAL_ARRAY:  return 1;
    case VAL_OBJECT: return 1;
    case VAL_FUNCTION: return 1;
    }
    return 0;
}

int value_equals(Value *a, Value *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (a->type != b->type) return 0;

    switch (a->type) {
    case VAL_NUMBER: return a->as.number == b->as.number;
    case VAL_STRING: return strcmp(a->as.string, b->as.string) == 0;
    case VAL_BOOL:   return a->as.boolean == b->as.boolean;
    case VAL_NULL:   return 1;
    default:         return a == b; /* reference equality for complex types */
    }
}

/* Array operations */
void value_array_push(Value *arr, Value *item) {
    if (arr->type != VAL_ARRAY) return;
    if (arr->as.array.length >= arr->as.array.capacity) {
        arr->as.array.capacity *= 2;
        arr->as.array.items = realloc(arr->as.array.items,
                                       arr->as.array.capacity * sizeof(Value *));
    }
    value_retain(item);
    arr->as.array.items[arr->as.array.length++] = item;
}

Value *value_array_pop(Value *arr) {
    if (arr->type != VAL_ARRAY || arr->as.array.length == 0) return NULL;
    Value *item = arr->as.array.items[--arr->as.array.length];
    /* Caller takes ownership; don't release */
    return item;
}

Value *value_array_get(Value *arr, int index) {
    if (arr->type != VAL_ARRAY) return NULL;
    if (index < 0 || index >= arr->as.array.length) return NULL;
    return arr->as.array.items[index];
}

void value_array_set(Value *arr, int index, Value *item) {
    if (arr->type != VAL_ARRAY) return;
    if (index < 0 || index >= arr->as.array.length) return;
    value_retain(item);
    value_release(arr->as.array.items[index]);
    arr->as.array.items[index] = item;
}

/* Object operations */
void value_object_set(Value *obj, const char *key, Value *val) {
    if (obj->type != VAL_OBJECT) return;

    /* Check if key already exists */
    for (int i = 0; i < obj->as.object.length; i++) {
        if (strcmp(obj->as.object.keys[i], key) == 0) {
            value_retain(val);
            value_release(obj->as.object.values[i]);
            obj->as.object.values[i] = val;
            return;
        }
    }

    /* New key */
    if (obj->as.object.length >= obj->as.object.capacity) {
        obj->as.object.capacity *= 2;
        obj->as.object.keys = realloc(obj->as.object.keys,
                                       obj->as.object.capacity * sizeof(char *));
        obj->as.object.values = realloc(obj->as.object.values,
                                         obj->as.object.capacity * sizeof(Value *));
    }
    obj->as.object.keys[obj->as.object.length] = strdup(key);
    value_retain(val);
    obj->as.object.values[obj->as.object.length] = val;
    obj->as.object.length++;
}

Value *value_object_get(Value *obj, const char *key) {
    if (obj->type != VAL_OBJECT) return NULL;
    for (int i = 0; i < obj->as.object.length; i++) {
        if (strcmp(obj->as.object.keys[i], key) == 0) {
            return obj->as.object.values[i];
        }
    }
    return NULL;
}

int value_object_has(Value *obj, const char *key) {
    if (obj->type != VAL_OBJECT) return 0;
    for (int i = 0; i < obj->as.object.length; i++) {
        if (strcmp(obj->as.object.keys[i], key) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ---- JSON serialization ---- */

static void json_append(char **buf, int *len, int *cap, const char *s) {
    int slen = (int)strlen(s);
    while (*len + slen + 1 >= *cap) {
        *cap *= 2;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}

static void json_append_char(char **buf, int *len, int *cap, char c) {
    if (*len + 2 >= *cap) {
        *cap *= 2;
        *buf = realloc(*buf, *cap);
    }
    (*buf)[(*len)++] = c;
    (*buf)[*len] = '\0';
}

static void json_encode_string(char **buf, int *len, int *cap, const char *s) {
    json_append_char(buf, len, cap, '"');
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '"':  json_append(buf, len, cap, "\\\""); break;
        case '\\': json_append(buf, len, cap, "\\\\"); break;
        case '\n': json_append(buf, len, cap, "\\n"); break;
        case '\t': json_append(buf, len, cap, "\\t"); break;
        case '\r': json_append(buf, len, cap, "\\r"); break;
        default:   json_append_char(buf, len, cap, *p); break;
        }
    }
    json_append_char(buf, len, cap, '"');
}

static void value_to_json_impl(Value *v, char **buf, int *len, int *cap) {
    if (!v) { json_append(buf, len, cap, "null"); return; }

    switch (v->type) {
    case VAL_NULL:
        json_append(buf, len, cap, "null");
        break;
    case VAL_BOOL:
        json_append(buf, len, cap, v->as.boolean ? "true" : "false");
        break;
    case VAL_NUMBER: {
        char tmp[64];
        if (v->as.number == (long long)v->as.number && fabs(v->as.number) < 1e15) {
            snprintf(tmp, sizeof(tmp), "%lld", (long long)v->as.number);
        } else {
            snprintf(tmp, sizeof(tmp), "%.14g", v->as.number);
        }
        json_append(buf, len, cap, tmp);
        break;
    }
    case VAL_STRING:
        json_encode_string(buf, len, cap, v->as.string);
        break;
    case VAL_ARRAY:
        json_append_char(buf, len, cap, '[');
        for (int i = 0; i < v->as.array.length; i++) {
            if (i > 0) json_append(buf, len, cap, ", ");
            value_to_json_impl(v->as.array.items[i], buf, len, cap);
        }
        json_append_char(buf, len, cap, ']');
        break;
    case VAL_OBJECT:
        json_append_char(buf, len, cap, '{');
        for (int i = 0; i < v->as.object.length; i++) {
            if (i > 0) json_append(buf, len, cap, ", ");
            json_encode_string(buf, len, cap, v->as.object.keys[i]);
            json_append(buf, len, cap, ": ");
            value_to_json_impl(v->as.object.values[i], buf, len, cap);
        }
        json_append_char(buf, len, cap, '}');
        break;
    case VAL_FUNCTION:
        json_append(buf, len, cap, "\"<function>\"");
        break;
    }
}

char *value_to_json(Value *v) {
    int len = 0, cap = 256;
    char *buf = malloc(cap);
    buf[0] = '\0';
    value_to_json_impl(v, &buf, &len, &cap);
    return buf;
}

/* ---- JSON parsing ---- */

static void skip_ws(const char *json, int *pos) {
    while (json[*pos] && (json[*pos] == ' ' || json[*pos] == '\t' ||
           json[*pos] == '\n' || json[*pos] == '\r'))
        (*pos)++;
}

Value *value_from_json(const char *json, int *pos) {
    skip_ws(json, pos);
    if (!json[*pos]) return value_new_null();

    char c = json[*pos];

    /* String */
    if (c == '"') {
        (*pos)++; /* skip opening quote */
        int cap = 64, len = 0;
        char *s = malloc(cap);
        while (json[*pos] && json[*pos] != '"') {
            if (json[*pos] == '\\') {
                (*pos)++;
                switch (json[*pos]) {
                case 'n': s[len++] = '\n'; break;
                case 't': s[len++] = '\t'; break;
                case 'r': s[len++] = '\r'; break;
                case '"': s[len++] = '"'; break;
                case '\\': s[len++] = '\\'; break;
                default: s[len++] = json[*pos]; break;
                }
            } else {
                s[len++] = json[*pos];
            }
            if (len >= cap - 1) { cap *= 2; s = realloc(s, cap); }
            (*pos)++;
        }
        s[len] = '\0';
        if (json[*pos] == '"') (*pos)++;
        return value_new_string_owned(s);
    }

    /* Number */
    if (c == '-' || isdigit((unsigned char)c)) {
        char *end;
        double n = strtod(json + *pos, &end);
        *pos = (int)(end - json);
        return value_new_number(n);
    }

    /* true */
    if (strncmp(json + *pos, "true", 4) == 0) {
        *pos += 4;
        return value_new_bool(1);
    }

    /* false */
    if (strncmp(json + *pos, "false", 5) == 0) {
        *pos += 5;
        return value_new_bool(0);
    }

    /* null */
    if (strncmp(json + *pos, "null", 4) == 0) {
        *pos += 4;
        return value_new_null();
    }

    /* Array */
    if (c == '[') {
        (*pos)++;
        Value *arr = value_new_array();
        skip_ws(json, pos);
        if (json[*pos] == ']') { (*pos)++; return arr; }

        while (1) {
            Value *item = value_from_json(json, pos);
            value_array_push(arr, item);
            value_release(item);
            skip_ws(json, pos);
            if (json[*pos] == ',') { (*pos)++; continue; }
            if (json[*pos] == ']') { (*pos)++; break; }
            break; /* malformed */
        }
        return arr;
    }

    /* Object */
    if (c == '{') {
        (*pos)++;
        Value *obj = value_new_object();
        skip_ws(json, pos);
        if (json[*pos] == '}') { (*pos)++; return obj; }

        while (1) {
            skip_ws(json, pos);
            /* Parse key (must be string) */
            if (json[*pos] != '"') break;
            Value *key_val = value_from_json(json, pos);
            char *key = strdup(key_val->as.string);
            value_release(key_val);

            skip_ws(json, pos);
            if (json[*pos] == ':') (*pos)++;

            Value *val = value_from_json(json, pos);
            value_object_set(obj, key, val);
            value_release(val);
            free(key);

            skip_ws(json, pos);
            if (json[*pos] == ',') { (*pos)++; continue; }
            if (json[*pos] == '}') { (*pos)++; break; }
            break;
        }
        return obj;
    }

    return value_new_null();
}
