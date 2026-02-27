#include "interpreter.h"
#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

/* ---- helpers ---- */

static ExecResult ok_val(Value *v) {
    ExecResult r = { STATUS_OK, v };
    return r;
}

static ExecResult ok_null(void) {
    ExecResult r = { STATUS_OK, value_new_null() };
    return r;
}

static ExecResult status_only(ExecStatus s) {
    ExecResult r = { s, NULL };
    return r;
}

static void runtime_error(Interpreter *interp, int line, const char *fmt, ...) {
    (void)interp;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "jung runtime error");
    if (line > 0) fprintf(stderr, " [line %d]", line);
    fprintf(stderr, ": %s\n", buf);
    exit(1);
}

/* ---- call user-defined function ---- */

static ExecResult call_function(Interpreter *interp, Value *fn, Value **args, int argc) {
    if (fn->type != VAL_FUNCTION) {
        ExecResult r = { STATUS_THROW, value_new_string("not a function") };
        return r;
    }

    interp->call_depth++;
    if (interp->call_depth > 200) {
        interp->call_depth--;
        runtime_error(interp, 0, "stack overflow (max 200 call depth)");
    }

    Table *saved = interp->variables;
    interp->variables = table_copy(saved);

    for (int i = 0; i < fn->as.function.param_count; i++) {
        if (i < argc) {
            table_set(interp->variables, fn->as.function.params[i], args[i]);
        } else {
            Value *null = value_new_null();
            table_set(interp->variables, fn->as.function.params[i], null);
            value_release(null);
        }
    }

    ExecResult r = exec_stmts(interp, fn->as.function.body_stmts, fn->as.function.body_count);

    if (r.status == STATUS_RETURN) r.status = STATUS_OK;
    if (!r.value) r.value = value_new_null();

    table_free(interp->variables);
    interp->variables = saved;
    interp->call_depth--;

    return r;
}

/* ---- exec_node: evaluate any AST node ---- */

ExecResult exec_node(Interpreter *interp, ASTNode *node) {
    if (!node) return ok_null();

    switch (node->type) {

    case NODE_NUMBER:
        return ok_val(value_new_number(node->as.number));

    case NODE_STRING:
        return ok_val(value_new_string(node->as.string));

    case NODE_BOOL:
        return ok_val(value_new_bool(node->as.boolean));

    case NODE_NULL:
        return ok_null();

    case NODE_THIS: {
        if (interp->current_instance) {
            value_retain(interp->current_instance);
            return ok_val(interp->current_instance);
        }
        return ok_null();
    }

    case NODE_VARIABLE: {
        Value *v = table_get(interp->variables, node->as.var_name);
        if (v) { value_retain(v); return ok_val(v); }
        v = table_get(interp->functions, node->as.var_name);
        if (v) { value_retain(v); return ok_val(v); }
        runtime_error(interp, node->line, "undefined variable '%s'", node->as.var_name);
        return ok_null();
    }

    case NODE_ARRAY: {
        Value *arr = value_new_array();
        for (int i = 0; i < node->as.array.count; i++) {
            ExecResult r = exec_node(interp, node->as.array.elements[i]);
            if (r.status != STATUS_OK) { value_release(arr); return r; }
            value_array_push(arr, r.value);
            value_release(r.value);
        }
        return ok_val(arr);
    }

    case NODE_OBJECT: {
        Value *obj = value_new_object();
        for (int i = 0; i < node->as.object.count; i++) {
            ExecResult r = exec_node(interp, node->as.object.values[i]);
            if (r.status != STATUS_OK) { value_release(obj); return r; }
            value_object_set(obj, node->as.object.keys[i], r.value);
            value_release(r.value);
        }
        return ok_val(obj);
    }

    case NODE_BINARY_OP: {
        TokenType op = node->as.binary.op;

        /* Short-circuit for and/or */
        if (op == TOKEN_AND) {
            ExecResult lr = exec_node(interp, node->as.binary.left);
            if (lr.status != STATUS_OK) return lr;
            if (!value_is_truthy(lr.value)) {
                value_release(lr.value);
                return ok_val(value_new_bool(0));
            }
            value_release(lr.value);
            ExecResult rr = exec_node(interp, node->as.binary.right);
            if (rr.status != STATUS_OK) return rr;
            int t = value_is_truthy(rr.value);
            value_release(rr.value);
            return ok_val(value_new_bool(t));
        }
        if (op == TOKEN_OR) {
            ExecResult lr = exec_node(interp, node->as.binary.left);
            if (lr.status != STATUS_OK) return lr;
            if (value_is_truthy(lr.value)) return lr;
            value_release(lr.value);
            return exec_node(interp, node->as.binary.right);
        }

        ExecResult lr = exec_node(interp, node->as.binary.left);
        if (lr.status != STATUS_OK) return lr;
        ExecResult rr = exec_node(interp, node->as.binary.right);
        if (rr.status != STATUS_OK) { value_release(lr.value); return rr; }

        Value *left = lr.value;
        Value *right = rr.value;

        /* String concatenation */
        if (op == TOKEN_PLUS && (left->type == VAL_STRING || right->type == VAL_STRING)) {
            char *ls = value_to_string(left);
            char *rs = value_to_string(right);
            int llen = (int)strlen(ls);
            int rlen = (int)strlen(rs);
            char *out = malloc(llen + rlen + 1);
            memcpy(out, ls, llen);
            memcpy(out + llen, rs, rlen);
            out[llen + rlen] = '\0';
            free(ls); free(rs);
            value_release(left); value_release(right);
            return ok_val(value_new_string_owned(out));
        }

        /* Numeric ops */
        if (left->type == VAL_NUMBER && right->type == VAL_NUMBER) {
            double l = left->as.number, r = right->as.number;
            value_release(left); value_release(right);
            switch (op) {
                case TOKEN_PLUS:     return ok_val(value_new_number(l + r));
                case TOKEN_MINUS:    return ok_val(value_new_number(l - r));
                case TOKEN_MULTIPLY: return ok_val(value_new_number(l * r));
                case TOKEN_DIVIDE:
                    if (r == 0) runtime_error(interp, node->line, "division by zero");
                    return ok_val(value_new_number(l / r));
                case TOKEN_MODULO:
                    if (r == 0) runtime_error(interp, node->line, "modulo by zero");
                    return ok_val(value_new_number(fmod(l, r)));
                case TOKEN_GT:  return ok_val(value_new_bool(l > r));
                case TOKEN_LT:  return ok_val(value_new_bool(l < r));
                case TOKEN_GTE: return ok_val(value_new_bool(l >= r));
                case TOKEN_LTE: return ok_val(value_new_bool(l <= r));
                case TOKEN_EQ:  return ok_val(value_new_bool(l == r));
                case TOKEN_NEQ: return ok_val(value_new_bool(l != r));
                default: break;
            }
        }

        /* Equality for non-numbers */
        if (op == TOKEN_EQ) {
            int eq = value_equals(left, right);
            value_release(left); value_release(right);
            return ok_val(value_new_bool(eq));
        }
        if (op == TOKEN_NEQ) {
            int eq = value_equals(left, right);
            value_release(left); value_release(right);
            return ok_val(value_new_bool(!eq));
        }

        value_release(left); value_release(right);
        runtime_error(interp, node->line, "unsupported operand types for binary op");
        return ok_null();
    }

    case NODE_UNARY_OP: {
        ExecResult r = exec_node(interp, node->as.unary.operand);
        if (r.status != STATUS_OK) return r;
        if (node->as.unary.op == TOKEN_MINUS) {
            if (r.value->type != VAL_NUMBER)
                runtime_error(interp, node->line, "unary minus requires number");
            double n = -r.value->as.number;
            value_release(r.value);
            return ok_val(value_new_number(n));
        }
        if (node->as.unary.op == TOKEN_NOT) {
            int t = !value_is_truthy(r.value);
            value_release(r.value);
            return ok_val(value_new_bool(t));
        }
        value_release(r.value);
        return ok_null();
    }

    case NODE_TERNARY: {
        ExecResult cr = exec_node(interp, node->as.ternary.cond);
        if (cr.status != STATUS_OK) return cr;
        int truthy = value_is_truthy(cr.value);
        value_release(cr.value);
        return exec_node(interp, truthy ? node->as.ternary.then_expr : node->as.ternary.else_expr);
    }

    case NODE_STRING_INTERP: {
        int cap = 256, len = 0;
        char *buf = malloc(cap);
        buf[0] = '\0';
        for (int i = 0; i < node->as.interp.count; i++) {
            ExecResult r = exec_node(interp, node->as.interp.parts[i]);
            if (r.status != STATUS_OK) { free(buf); return r; }
            char *s = value_to_string(r.value);
            int slen = (int)strlen(s);
            while (len + slen + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            memcpy(buf + len, s, slen);
            len += slen;
            free(s);
            value_release(r.value);
        }
        buf[len] = '\0';
        return ok_val(value_new_string_owned(buf));
    }

    case NODE_INDEX: {
        ExecResult ar = exec_node(interp, node->as.index.object);
        if (ar.status != STATUS_OK) return ar;
        ExecResult ir = exec_node(interp, node->as.index.index);
        if (ir.status != STATUS_OK) { value_release(ar.value); return ir; }

        Value *container = ar.value;
        Value *idx = ir.value;

        if (container->type == VAL_ARRAY && idx->type == VAL_NUMBER) {
            int i = (int)idx->as.number;
            if (i < 0) i += container->as.array.length;
            Value *item = value_array_get(container, i);
            Value *result = item ? (value_retain(item), item) : value_new_null();
            value_release(container); value_release(idx);
            return ok_val(result);
        }
        if (container->type == VAL_OBJECT && idx->type == VAL_STRING) {
            Value *item = value_object_get(container, idx->as.string);
            Value *result = item ? (value_retain(item), item) : value_new_null();
            value_release(container); value_release(idx);
            return ok_val(result);
        }
        if (container->type == VAL_STRING && idx->type == VAL_NUMBER) {
            int i = (int)idx->as.number;
            int slen = (int)strlen(container->as.string);
            if (i < 0) i += slen;
            Value *result;
            if (i >= 0 && i < slen) {
                char ch[2] = { container->as.string[i], '\0' };
                result = value_new_string(ch);
            } else {
                result = value_new_null();
            }
            value_release(container); value_release(idx);
            return ok_val(result);
        }
        value_release(container); value_release(idx);
        return ok_null();
    }

    case NODE_DOT_ACCESS: {
        ExecResult r = exec_node(interp, node->as.dot.object);
        if (r.status != STATUS_OK) return r;
        Value *obj = r.value;

        if (strcmp(node->as.dot.field, "length") == 0) {
            double len = 0;
            if (obj->type == VAL_STRING) len = strlen(obj->as.string);
            else if (obj->type == VAL_ARRAY) len = obj->as.array.length;
            else if (obj->type == VAL_OBJECT) len = obj->as.object.length;
            value_release(obj);
            return ok_val(value_new_number(len));
        }

        if (obj->type == VAL_OBJECT) {
            Value *v = value_object_get(obj, node->as.dot.field);
            if (v) {
                value_retain(v);
                value_release(obj);
                return ok_val(v);
            }
        }
        value_release(obj);
        return ok_null();
    }

    case NODE_METHOD_CALL: {
        ExecResult r = exec_node(interp, node->as.method.object);
        if (r.status != STATUS_OK) return r;
        Value *obj = r.value;

        /* Evaluate arguments */
        int argc = node->as.method.arg_count;
        Value **args = NULL;
        if (argc > 0) {
            args = malloc(argc * sizeof(Value *));
            for (int i = 0; i < argc; i++) {
                ExecResult ar = exec_node(interp, node->as.method.args[i]);
                if (ar.status != STATUS_OK) {
                    for (int j = 0; j < i; j++) value_release(args[j]);
                    free(args);
                    value_release(obj);
                    return ar;
                }
                args[i] = ar.value;
            }
        }

        /* Check class methods first */
        if (obj->type == VAL_OBJECT) {
            Value *class_name = value_object_get(obj, "__class__");
            if (class_name && class_name->type == VAL_STRING) {
                Value *class_obj = table_get(interp->classes, class_name->as.string);
                if (class_obj && class_obj->type == VAL_OBJECT) {
                    Value *method = value_object_get(class_obj, node->as.method.method);
                    if (method && method->type == VAL_FUNCTION) {
                        Value *saved_instance = interp->current_instance;
                        interp->current_instance = obj;
                        ExecResult mr = call_function(interp, method, args, argc);
                        interp->current_instance = saved_instance;
                        for (int i = 0; i < argc; i++) value_release(args[i]);
                        free(args);
                        value_release(obj);
                        return mr;
                    }
                }
            }
        }

        /* Try builtin methods */
        ExecResult mr;
        if (builtin_method(interp, obj, node->as.method.method, args, argc, &mr)) {
            for (int i = 0; i < argc; i++) value_release(args[i]);
            free(args);
            value_release(obj);
            return mr;
        }

        for (int i = 0; i < argc; i++) value_release(args[i]);
        free(args);
        runtime_error(interp, node->line, "unknown method '%s'", node->as.method.method);
        value_release(obj);
        return ok_null();
    }

    case NODE_FUNCTION_CALL: {
        char *name = node->as.func_call.name;
        int argc = node->as.func_call.arg_count;

        /* Evaluate arguments */
        Value **args = NULL;
        if (argc > 0) {
            args = malloc(argc * sizeof(Value *));
            for (int i = 0; i < argc; i++) {
                ExecResult r = exec_node(interp, node->as.func_call.args[i]);
                if (r.status != STATUS_OK) {
                    for (int j = 0; j < i; j++) value_release(args[j]);
                    free(args);
                    return r;
                }
                args[i] = r.value;
            }
        }

        /* Try builtins first */
        ExecResult br;
        if (builtin_call(interp, name, args, argc, &br)) {
            for (int i = 0; i < argc; i++) value_release(args[i]);
            free(args);
            return br;
        }

        /* Try user-defined functions */
        Value *fn = table_get(interp->functions, name);
        if (fn && fn->type == VAL_FUNCTION) {
            ExecResult fr = call_function(interp, fn, args, argc);
            for (int i = 0; i < argc; i++) value_release(args[i]);
            free(args);
            return fr;
        }

        /* Try variables holding functions */
        Value *var = table_get(interp->variables, name);
        if (var && var->type == VAL_FUNCTION) {
            ExecResult fr = call_function(interp, var, args, argc);
            for (int i = 0; i < argc; i++) value_release(args[i]);
            free(args);
            return fr;
        }

        for (int i = 0; i < argc; i++) value_release(args[i]);
        free(args);
        runtime_error(interp, node->line, "undefined function '%s'", name);
        return ok_null();
    }

    case NODE_NEW_INSTANCE: {
        char *cname = node->as.new_inst.class_name;
        Value *class_obj = table_get(interp->classes, cname);
        if (!class_obj || class_obj->type != VAL_OBJECT)
            runtime_error(interp, node->line, "undefined class '%s'", cname);

        Value *instance = value_new_object();
        Value *cn = value_new_string(cname);
        value_object_set(instance, "__class__", cn);
        value_release(cn);

        /* Evaluate constructor args */
        int argc = node->as.new_inst.arg_count;
        Value **args = NULL;
        if (argc > 0) {
            args = malloc(argc * sizeof(Value *));
            for (int i = 0; i < argc; i++) {
                ExecResult r = exec_node(interp, node->as.new_inst.args[i]);
                if (r.status != STATUS_OK) {
                    for (int j = 0; j < i; j++) value_release(args[j]);
                    free(args);
                    value_release(instance);
                    return r;
                }
                args[i] = r.value;
            }
        }

        /* Call constructor if exists */
        Value *ctor = value_object_get(class_obj, "constructor");
        if (!ctor) ctor = value_object_get(class_obj, "init");
        if (ctor && ctor->type == VAL_FUNCTION) {
            Value *saved = interp->current_instance;
            interp->current_instance = instance;
            ExecResult cr = call_function(interp, ctor, args, argc);
            interp->current_instance = saved;
            if (cr.value) value_release(cr.value);
        }

        for (int i = 0; i < argc; i++) value_release(args[i]);
        free(args);
        return ok_val(instance);
    }

    /* ---- statement nodes handled in exec_node ---- */

    case NODE_PRINT: {
        ExecResult r = exec_node(interp, node->as.print_stmt.expr);
        if (r.status != STATUS_OK) return r;
        char *s = value_to_string(r.value);
        printf("%s\n", s);
        free(s);
        value_release(r.value);
        return ok_null();
    }

    case NODE_ASSIGNMENT: {
        ExecResult r = exec_node(interp, node->as.assign.value);
        if (r.status != STATUS_OK) return r;
        table_set(interp->variables, node->as.assign.name, r.value);
        value_release(r.value);
        return ok_null();
    }

    case NODE_COMPOUND_ASSIGN: {
        Value *current = table_get(interp->variables, node->as.compound.name);
        if (!current)
            runtime_error(interp, node->line, "undefined variable '%s'", node->as.compound.name);

        ExecResult r = exec_node(interp, node->as.compound.value);
        if (r.status != STATUS_OK) return r;

        Value *result = NULL;
        if (current->type == VAL_NUMBER && r.value->type == VAL_NUMBER) {
            double l = current->as.number, rv = r.value->as.number;
            switch (node->as.compound.op) {
                case TOKEN_PLUS_ASSIGN:     result = value_new_number(l + rv); break;
                case TOKEN_MINUS_ASSIGN:    result = value_new_number(l - rv); break;
                case TOKEN_MULTIPLY_ASSIGN: result = value_new_number(l * rv); break;
                case TOKEN_DIVIDE_ASSIGN:
                    if (rv == 0) runtime_error(interp, node->line, "division by zero");
                    result = value_new_number(l / rv); break;
                default: result = value_new_null(); break;
            }
        } else if (node->as.compound.op == TOKEN_PLUS_ASSIGN &&
                   (current->type == VAL_STRING || r.value->type == VAL_STRING)) {
            char *ls = value_to_string(current);
            char *rs = value_to_string(r.value);
            int llen = (int)strlen(ls), rlen = (int)strlen(rs);
            char *out = malloc(llen + rlen + 1);
            memcpy(out, ls, llen);
            memcpy(out + llen, rs, rlen);
            out[llen + rlen] = '\0';
            free(ls); free(rs);
            result = value_new_string_owned(out);
        } else {
            runtime_error(interp, node->line, "unsupported types for compound assignment");
            result = value_new_null();
        }

        value_release(r.value);
        table_set(interp->variables, node->as.compound.name, result);
        value_release(result);
        return ok_null();
    }

    case NODE_DOT_ASSIGN: {
        if (node->as.dot_assign.is_bracket) {
            /* Bracket assignment: object is a NODE_INDEX containing container + index expr */
            ASTNode *idx_node = node->as.dot_assign.object;
            ExecResult cr = exec_node(interp, idx_node->as.index.object);
            if (cr.status != STATUS_OK) return cr;
            ExecResult ir = exec_node(interp, idx_node->as.index.index);
            if (ir.status != STATUS_OK) { value_release(cr.value); return ir; }
            ExecResult vr = exec_node(interp, node->as.dot_assign.value);
            if (vr.status != STATUS_OK) { value_release(cr.value); value_release(ir.value); return vr; }

            if (cr.value->type == VAL_ARRAY && ir.value->type == VAL_NUMBER) {
                int i = (int)ir.value->as.number;
                if (i < 0) i += cr.value->as.array.length;
                value_array_set(cr.value, i, vr.value);
            } else if (cr.value->type == VAL_OBJECT && ir.value->type == VAL_STRING) {
                value_object_set(cr.value, ir.value->as.string, vr.value);
            }
            value_release(cr.value);
            value_release(ir.value);
            value_release(vr.value);
        } else {
            /* Dot assignment: obj.field = value */
            ExecResult or_ = exec_node(interp, node->as.dot_assign.object);
            if (or_.status != STATUS_OK) return or_;
            ExecResult vr = exec_node(interp, node->as.dot_assign.value);
            if (vr.status != STATUS_OK) { value_release(or_.value); return vr; }

            if (or_.value->type == VAL_OBJECT) {
                value_object_set(or_.value, node->as.dot_assign.field, vr.value);
            }
            value_release(or_.value);
            value_release(vr.value);
        }
        return ok_null();
    }

    case NODE_IF: {
        ExecResult cr = exec_node(interp, node->as.if_stmt.cond);
        if (cr.status != STATUS_OK) return cr;
        int truthy = value_is_truthy(cr.value);
        value_release(cr.value);

        if (truthy) {
            return exec_stmts(interp, node->as.if_stmt.then_stmts, node->as.if_stmt.then_count);
        } else if (node->as.if_stmt.else_stmts) {
            return exec_stmts(interp, node->as.if_stmt.else_stmts, node->as.if_stmt.else_count);
        }
        return ok_null();
    }

    case NODE_WHILE: {
        while (1) {
            ExecResult cr = exec_node(interp, node->as.while_loop.cond);
            if (cr.status != STATUS_OK) return cr;
            int truthy = value_is_truthy(cr.value);
            value_release(cr.value);
            if (!truthy) break;

            ExecResult br = exec_stmts(interp, node->as.while_loop.body, node->as.while_loop.body_count);
            if (br.value) value_release(br.value);
            if (br.status == STATUS_BREAK) break;
            if (br.status == STATUS_RETURN) return br;
            if (br.status == STATUS_THROW) return br;
            /* STATUS_CONTINUE just proceeds to next iteration */
        }
        return ok_null();
    }

    case NODE_FOR: {
        ExecResult ir = exec_node(interp, node->as.for_loop.iterable);
        if (ir.status != STATUS_OK) return ir;
        Value *iterable = ir.value;

        if (iterable->type == VAL_ARRAY) {
            for (int i = 0; i < iterable->as.array.length; i++) {
                table_set(interp->variables, node->as.for_loop.var, iterable->as.array.items[i]);
                ExecResult br = exec_stmts(interp, node->as.for_loop.body, node->as.for_loop.body_count);
                if (br.value) value_release(br.value);
                if (br.status == STATUS_BREAK) break;
                if (br.status == STATUS_RETURN) { value_release(iterable); return br; }
                if (br.status == STATUS_THROW) { value_release(iterable); return br; }
            }
        } else if (iterable->type == VAL_STRING) {
            int slen = (int)strlen(iterable->as.string);
            for (int i = 0; i < slen; i++) {
                char ch[2] = { iterable->as.string[i], '\0' };
                Value *cv = value_new_string(ch);
                table_set(interp->variables, node->as.for_loop.var, cv);
                value_release(cv);
                ExecResult br = exec_stmts(interp, node->as.for_loop.body, node->as.for_loop.body_count);
                if (br.value) value_release(br.value);
                if (br.status == STATUS_BREAK) break;
                if (br.status == STATUS_RETURN) { value_release(iterable); return br; }
                if (br.status == STATUS_THROW) { value_release(iterable); return br; }
            }
        } else if (iterable->type == VAL_OBJECT) {
            for (int i = 0; i < iterable->as.object.length; i++) {
                Value *kv = value_new_string(iterable->as.object.keys[i]);
                table_set(interp->variables, node->as.for_loop.var, kv);
                value_release(kv);
                ExecResult br = exec_stmts(interp, node->as.for_loop.body, node->as.for_loop.body_count);
                if (br.value) value_release(br.value);
                if (br.status == STATUS_BREAK) break;
                if (br.status == STATUS_RETURN) { value_release(iterable); return br; }
                if (br.status == STATUS_THROW) { value_release(iterable); return br; }
            }
        }
        value_release(iterable);
        return ok_null();
    }

    case NODE_FUNCTION_DEF: {
        Value *fn = value_new_function(
            node->as.func_def.name,
            node->as.func_def.params,
            node->as.func_def.param_count,
            node->as.func_def.body,
            node->as.func_def.body_count
        );
        table_set(interp->functions, node->as.func_def.name, fn);
        value_release(fn);
        return ok_null();
    }

    case NODE_RETURN: {
        if (node->as.ret.value) {
            ExecResult r = exec_node(interp, node->as.ret.value);
            if (r.status != STATUS_OK) return r;
            ExecResult ret = { STATUS_RETURN, r.value };
            return ret;
        }
        return status_only(STATUS_RETURN);
    }

    case NODE_BREAK:
        return status_only(STATUS_BREAK);

    case NODE_CONTINUE:
        return status_only(STATUS_CONTINUE);

    case NODE_CLASS_DEF: {
        Value *class_obj = value_new_object();
        for (int i = 0; i < node->as.class_def.method_count; i++) {
            ASTNode *m = node->as.class_def.methods[i];
            Value *fn = value_new_function(
                m->as.func_def.name,
                m->as.func_def.params,
                m->as.func_def.param_count,
                m->as.func_def.body,
                m->as.func_def.body_count
            );
            value_object_set(class_obj, m->as.func_def.name, fn);
            value_release(fn);
        }
        table_set(interp->classes, node->as.class_def.name, class_obj);
        value_release(class_obj);
        return ok_null();
    }

    case NODE_TRY_CATCH: {
        /* Execute try block */
        ExecResult tr = exec_stmts(interp, node->as.try_catch.try_stmts, node->as.try_catch.try_count);
        if (tr.status == STATUS_THROW) {
            /* Caught: bind error to catch variable */
            if (node->as.try_catch.catch_var && tr.value) {
                table_set(interp->variables, node->as.try_catch.catch_var, tr.value);
            }
            if (tr.value) value_release(tr.value);
            return exec_stmts(interp, node->as.try_catch.catch_stmts, node->as.try_catch.catch_count);
        }
        return tr;
    }

    case NODE_THROW: {
        ExecResult r = exec_node(interp, node->as.throw_stmt.value);
        if (r.status != STATUS_OK) return r;
        ExecResult thr = { STATUS_THROW, r.value };
        return thr;
    }

    case NODE_IMPORT: {
        const char *path = node->as.import_stmt.path;

        /* Check already imported */
        for (int i = 0; i < interp->import_depth; i++) {
            if (strcmp(interp->import_stack[i], path) == 0)
                return ok_null();
        }
        if (interp->import_depth >= 32)
            runtime_error(interp, node->line, "too many imports");

        interp->import_stack[interp->import_depth++] = strdup(path);

        FILE *f = fopen(path, "r");
        if (!f) runtime_error(interp, node->line, "cannot open import '%s'", path);
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        rewind(f);
        char *src = malloc(sz + 1);
        fread(src, 1, sz, f);
        src[sz] = '\0';
        fclose(f);

        interpreter_run_source(interp, src);
        free(src);
        return ok_null();
    }

    case NODE_PROGRAM:
        return exec_stmts(interp, node->as.program.stmts, node->as.program.count);

    default:
        return ok_null();
    }
}

/* ---- exec_stmts ---- */

ExecResult exec_stmts(Interpreter *interp, ASTNode **stmts, int count) {
    ExecResult last = { STATUS_OK, NULL };
    for (int i = 0; i < count; i++) {
        if (last.value) { value_release(last.value); last.value = NULL; }
        last = exec_node(interp, stmts[i]);
        if (last.status != STATUS_OK) return last;
    }
    if (!last.value) last.value = value_new_null();
    return last;
}

/* ---- public API ---- */

void interpreter_init(Interpreter *interp) {
    memset(interp, 0, sizeof(Interpreter));
    interp->variables = table_new();
    interp->functions = table_new();
    interp->classes = table_new();
    interp->current_instance = NULL;
    interp->call_depth = 0;
    interp->import_depth = 0;
}

void interpreter_free(Interpreter *interp) {
    table_free(interp->variables);
    table_free(interp->functions);
    table_free(interp->classes);
    for (int i = 0; i < interp->import_depth; i++) {
        free(interp->import_stack[i]);
    }
}

int interpreter_run_source(Interpreter *interp, const char *source) {
    Lexer lex;
    lexer_init(&lex, source);
    lexer_tokenize(&lex);

    Parser parser;
    parser_init(&parser, lex.tokens, lex.count);
    ASTNode *program = parser_parse(&parser);

    if (program) {
        ExecResult r = exec_node(interp, program);
        if (r.value) value_release(r.value);
        ast_free(program);
    }

    lexer_free(&lex);
    return 0;
}

void interpreter_repl(Interpreter *interp) {
    printf("jung v0.1.0\n");
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
        parser_init(&parser, lex.tokens, lex.count);
        ASTNode *program = parser_parse(&parser);

        if (program && program->type == NODE_PROGRAM && program->as.program.count > 0) {
            if (program->as.program.count == 1) {
                ASTNode *stmt = program->as.program.stmts[0];
                if (stmt->type == NODE_PRINT || stmt->type == NODE_ASSIGNMENT ||
                    stmt->type == NODE_IF || stmt->type == NODE_WHILE ||
                    stmt->type == NODE_FOR || stmt->type == NODE_FUNCTION_DEF ||
                    stmt->type == NODE_CLASS_DEF || stmt->type == NODE_IMPORT ||
                    stmt->type == NODE_COMPOUND_ASSIGN || stmt->type == NODE_DOT_ASSIGN) {
                    ExecResult r = exec_node(interp, stmt);
                    if (r.value) value_release(r.value);
                } else {
                    ExecResult r = exec_node(interp, stmt);
                    if (r.status == STATUS_OK && r.value && r.value->type != VAL_NULL) {
                        char *s = value_to_string(r.value);
                        printf("%s\n", s);
                        free(s);
                    }
                    if (r.value) value_release(r.value);
                }
            } else {
                ExecResult r = exec_stmts(interp, program->as.program.stmts, program->as.program.count);
                if (r.value) value_release(r.value);
            }
        }

        ast_free(program);
        lexer_free(&lex);
    }
}
