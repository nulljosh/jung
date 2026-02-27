#include "table.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAP 16

static unsigned int hash_string(const char *s) {
    unsigned int h = 2166136261u;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    return h;
}

void table_init(Table *t) {
    t->cap = INITIAL_CAP;
    t->count = 0;
    t->refcount = 1;
    t->entries = calloc((size_t)t->cap, sizeof(TableEntry *));
}

void table_free(Table *t) {
    for (int i = 0; i < t->cap; i++) {
        TableEntry *e = t->entries[i];
        while (e) {
            TableEntry *next = e->next;
            free(e->key);
            val_free(&e->value);
            free(e);
            e = next;
        }
    }
    free(t->entries);
    t->entries = NULL;
    t->count = 0;
}

static void table_resize(Table *t) {
    int new_cap = t->cap * 2;
    TableEntry **new_entries = calloc((size_t)new_cap, sizeof(TableEntry *));

    for (int i = 0; i < t->cap; i++) {
        TableEntry *e = t->entries[i];
        while (e) {
            TableEntry *next = e->next;
            unsigned int idx = hash_string(e->key) % (unsigned int)new_cap;
            e->next = new_entries[idx];
            new_entries[idx] = e;
            e = next;
        }
    }
    free(t->entries);
    t->entries = new_entries;
    t->cap = new_cap;
}

void table_set(Table *t, const char *key, Value val) {
    if (t->count >= t->cap * 3 / 4) table_resize(t);

    unsigned int idx = hash_string(key) % (unsigned int)t->cap;
    TableEntry *e = t->entries[idx];
    while (e) {
        if (strcmp(e->key, key) == 0) {
            val_free(&e->value);
            e->value = val;
            return;
        }
        e = e->next;
    }

    TableEntry *ne = malloc(sizeof(TableEntry));
    ne->key = strdup(key);
    ne->value = val;
    ne->next = t->entries[idx];
    t->entries[idx] = ne;
    t->count++;
}

int table_get(Table *t, const char *key, Value *out) {
    unsigned int idx = hash_string(key) % (unsigned int)t->cap;
    TableEntry *e = t->entries[idx];
    while (e) {
        if (strcmp(e->key, key) == 0) {
            *out = e->value;
            return 1;
        }
        e = e->next;
    }
    return 0;
}

int table_has(Table *t, const char *key) {
    unsigned int idx = hash_string(key) % (unsigned int)t->cap;
    TableEntry *e = t->entries[idx];
    while (e) {
        if (strcmp(e->key, key) == 0) return 1;
        e = e->next;
    }
    return 0;
}

void table_delete(Table *t, const char *key) {
    unsigned int idx = hash_string(key) % (unsigned int)t->cap;
    TableEntry **pp = &t->entries[idx];
    while (*pp) {
        if (strcmp((*pp)->key, key) == 0) {
            TableEntry *del = *pp;
            *pp = del->next;
            free(del->key);
            val_free(&del->value);
            free(del);
            t->count--;
            return;
        }
        pp = &(*pp)->next;
    }
}

Value table_keys(Table *t) {
    Value arr = val_array(t->count > 0 ? t->count : 8);
    for (int i = 0; i < t->cap; i++) {
        TableEntry *e = t->entries[i];
        while (e) {
            val_array_push(&arr, val_string(e->key, (int)strlen(e->key)));
            e = e->next;
        }
    }
    return arr;
}

Value table_values(Table *t) {
    Value arr = val_array(t->count > 0 ? t->count : 8);
    for (int i = 0; i < t->cap; i++) {
        TableEntry *e = t->entries[i];
        while (e) {
            val_array_push(&arr, val_copy(e->value));
            e = e->next;
        }
    }
    return arr;
}
