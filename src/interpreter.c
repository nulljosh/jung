#include "interpreter.h"
#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

/* ---- error ---- */

static void throw_exception(Interpreter *it, const char *msg) {
    free(it->exception_msg);
    it->exception_msg = strdup(msg);
    it->exception_active = 1;
    if (it->try_depth > 0) {
        longjmp(it->try_stack[it->try_depth - 1], 1);
    }
    fprintf(stderr, "Uncaught exception: %s\n", msg);
    exit(1);
}

static void runtime_error(Interpreter *it, int line, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (it->try_depth > 0) {
        /* Inside a try block -- throw as exception */
        char msg[1200];
        if (line > 0) snprintf(msg, sizeof(msg), "[line %d] %s", line, buf);
        else snprintf(msg, sizeof(msg), "%s", buf);
        throw_exception(it, msg);
    }

    fprintf(stderr, "jit runtime error");
    if (line > 0) fprintf(stderr, " [line %d]", line);
    fprintf(stderr, ": %s\n", buf);
    exit(1);
}

/* ---- scope management ---- */

static void push_scope(Interpreter *it) {
    if (it->scope_depth >= MAX_SCOPES - 1) {
        runtime_error(it, 0, "scope overflow");
    }
    it->scope_depth++;
    table_init(&it->scopes[it->scope_depth].vars);
}

static void pop_scope(Interpreter *it) {
    if (it->scope_depth < 0) return;
    table_free(&it->scopes[it->scope_depth].vars);
    it->scope_depth--;
}

/* ---- variable access ---- */

int interp_get_var(Interpreter *it, const char *name, Value *out) {
    /* Search from current scope upward */
    for (int i = it->scope_depth; i >= 0; i--) {
        if (table_get(&it->scopes[i].vars, name, out)) return 1;
    }
    /* Check globals */
    if (table_get(&it->globals, name, out)) return 1;
    return 0;
}

void interp_set_var(Interpreter *it, const char *name, Value val) {
    /* Find existing var in any scope and update it */
    for (int i = it->scope_depth; i >= 0; i--) {
        Value dummy;
        if (table_get(&it->scopes[i].vars, name, &dummy)) {
            table_set(&it->scopes[i].vars, name, val);
            return;
        }
    }
    /* Check globals */
    Value dummy;
    if (table_get(&it->globals, name, &dummy)) {
        table_set(&it->globals, name, val);
        return;
    }
    /* Define in current scope */
    table_set(&it->scopes[it->scope_depth].vars, name, val);
}

void interp_def_var(Interpreter *it, const char *name, Value val) {
    /* Always define in current scope */
    table_set(&it->scopes[it->scope_depth].vars, name, val);
}

/* ---- forward declarations ---- */

static Value eval_node(Interpreter *it, ASTNode *node);
static void exec_stmts(Interpreter *it, ASTNode **stmts, int count);

/* ---- call user function ---- */

static Value call_function(Interpreter *it, FuncDef *fn, Value *args, int argc, int line) {
    if (it->call_depth >= MAX_CALL_DEPTH) {
        runtime_error(it, line, "stack overflow (max %d call depth)", MAX_CALL_DEPTH);
    }
    it->call_depth++;
    push_scope(it);

    /* Bind parameters */
    for (int i = 0; i < fn->param_count; i++) {
        if (i < argc) {
            interp_def_var(it, fn->params[i].name, val_copy(args[i]));
        } else if (fn->params[i].default_val) {
            Value def = eval_node(it, fn->params[i].default_val);
            interp_def_var(it, fn->params[i].name, def);
        } else {
            interp_def_var(it, fn->params[i].name, val_null());
        }
    }

    /* Execute body */
    it->return_flag = 0;
    it->return_value = val_null();

    exec_stmts(it, fn->body, fn->body_count);

    Value result = val_null();
    if (it->return_flag) {
        result = it->return_value;
        it->return_flag = 0;
        it->return_value = val_null();
    }

    pop_scope(it);
    it->call_depth--;
    return result;
}

/* ---- eval ---- */

static Value eval_node(Interpreter *it, ASTNode *node) {
    if (!node) return val_null();

    switch (node->type) {
    case NODE_NUMBER:
        return val_number(node->as.number);

    case NODE_STRING:
        return val_string(node->as.string.str, node->as.string.len);

    case NODE_BOOL:
        return val_bool(node->as.boolean);

    case NODE_NULL:
        return val_null();

    case NODE_THIS: {
        if (it->this_obj) return val_copy(*it->this_obj);
        return val_null();
    }

    case NODE_VARIABLE: {
        Value v;
        if (interp_get_var(it, node->as.var_name, &v)) {
            return val_copy(v);
        }
        /* Check functions */
        if (table_get(&it->functions, node->as.var_name, &v)) {
            return val_copy(v);
        }
        runtime_error(it, node->line, "undefined variable '%s'", node->as.var_name);
        return val_null();
    }

    case NODE_ARRAY: {
        Value arr = val_array(node->as.array.count > 0 ? node->as.array.count : 8);
        for (int i = 0; i < node->as.array.count; i++) {
            Value item = eval_node(it, node->as.array.elements[i]);
            val_array_push(&arr, item);
        }
        return arr;
    }

    case NODE_OBJECT: {
        Value obj = val_object();
        for (int i = 0; i < node->as.object.count; i++) {
            Value v = eval_node(it, node->as.object.values[i]);
            table_set(obj.as.object, node->as.object.keys[i], v);
        }
        return obj;
    }

    case NODE_BINARY: {
        Value left = eval_node(it, node->as.binary.left);
        TokenType op = node->as.binary.op;

        /* Short-circuit for and/or */
        if (op == TOKEN_AND) {
            if (!val_is_truthy(left)) { val_free(&left); return val_bool(0); }
            Value right = eval_node(it, node->as.binary.right);
            int result = val_is_truthy(right);
            val_free(&left); val_free(&right);
            return val_bool(result);
        }
        if (op == TOKEN_OR) {
            if (val_is_truthy(left)) return left;
            val_free(&left);
            return eval_node(it, node->as.binary.right);
        }

        Value right = eval_node(it, node->as.binary.right);

        /* String concatenation */
        if (op == TOKEN_PLUS && (left.type == VAL_STRING || right.type == VAL_STRING)) {
            char *ls = val_to_string(left);
            char *rs = val_to_string(right);
            int llen = (int)strlen(ls);
            int rlen = (int)strlen(rs);
            char *out = malloc((size_t)(llen + rlen + 1));
            memcpy(out, ls, (size_t)llen);
            memcpy(out + llen, rs, (size_t)rlen);
            out[llen + rlen] = '\0';
            free(ls); free(rs);
            val_free(&left); val_free(&right);
            return val_string_take(out, llen + rlen);
        }

        /* Numeric operations */
        if (left.type == VAL_NUMBER && right.type == VAL_NUMBER) {
            double l = left.as.number, r = right.as.number;
            switch (op) {
                case TOKEN_PLUS:     return val_number(l + r);
                case TOKEN_MINUS:    return val_number(l - r);
                case TOKEN_MULTIPLY: return val_number(l * r);
                case TOKEN_DIVIDE:
                    if (r == 0) runtime_error(it, node->line, "division by zero");
                    /* Integer division when both operands are integers */
                    if (l == floor(l) && r == floor(r)) {
                        return val_number((double)((long)l / (long)r));
                    }
                    return val_number(l / r);
                case TOKEN_MODULO:
                    if (r == 0) runtime_error(it, node->line, "modulo by zero");
                    return val_number(fmod(l, r));
                case TOKEN_GT:  return val_bool(l > r);
                case TOKEN_LT:  return val_bool(l < r);
                case TOKEN_GTE: return val_bool(l >= r);
                case TOKEN_LTE: return val_bool(l <= r);
                case TOKEN_EQ:  return val_bool(l == r);
                case TOKEN_NEQ: return val_bool(l != r);
                default: break;
            }
        }

        /* Equality for non-numbers */
        if (op == TOKEN_EQ) {
            int result = val_equal(left, right);
            val_free(&left); val_free(&right);
            return val_bool(result);
        }
        if (op == TOKEN_NEQ) {
            int result = !val_equal(left, right);
            val_free(&left); val_free(&right);
            return val_bool(result);
        }

        runtime_error(it, node->line, "unsupported operand types for binary op");
        return val_null();
    }

    case NODE_UNARY: {
        Value operand = eval_node(it, node->as.unary.operand);
        if (node->as.unary.op == TOKEN_MINUS) {
            if (operand.type != VAL_NUMBER)
                runtime_error(it, node->line, "unary minus requires number");
            return val_number(-operand.as.number);
        }
        if (node->as.unary.op == TOKEN_NOT) {
            int result = !val_is_truthy(operand);
            val_free(&operand);
            return val_bool(result);
        }
        return val_null();
    }

    case NODE_TERNARY: {
        Value cond = eval_node(it, node->as.ternary.condition);
        int truthy = val_is_truthy(cond);
        val_free(&cond);
        if (truthy) return eval_node(it, node->as.ternary.then_expr);
        return eval_node(it, node->as.ternary.else_expr);
    }

    case NODE_STRING_INTERP: {
        int cap = 256, len = 0;
        char *buf = malloc((size_t)cap);
        buf[0] = '\0';

        for (int i = 0; i < node->as.interp.count; i++) {
            Value part = eval_node(it, node->as.interp.parts[i]);
            char *s = val_to_string(part);
            int slen = (int)strlen(s);
            while (len + slen + 1 >= cap) { cap *= 2; buf = realloc(buf, (size_t)cap); }
            memcpy(buf + len, s, (size_t)slen);
            len += slen;
            free(s);
            val_free(&part);
        }
        buf[len] = '\0';
        return val_string_take(buf, len);
    }

    case NODE_ARRAY_INDEX: {
        Value arr = eval_node(it, node->as.array_index.array_expr);
        Value idx = eval_node(it, node->as.array_index.index);

        if (arr.type == VAL_ARRAY && idx.type == VAL_NUMBER) {
            int i = (int)idx.as.number;
            if (i < 0) i += arr.as.array.count;
            Value result = val_null();
            if (i >= 0 && i < arr.as.array.count) {
                result = val_copy(arr.as.array.items[i]);
            }
            val_free(&arr); val_free(&idx);
            return result;
        }
        if (arr.type == VAL_OBJECT && idx.type == VAL_STRING) {
            Value result = val_null();
            Value v;
            if (table_get(arr.as.object, idx.as.string.str, &v)) {
                result = val_copy(v);
            }
            val_free(&arr); val_free(&idx);
            return result;
        }
        if (arr.type == VAL_STRING && idx.type == VAL_NUMBER) {
            int i = (int)idx.as.number;
            if (i < 0) i += arr.as.string.len;
            if (i >= 0 && i < arr.as.string.len) {
                Value result = val_string(arr.as.string.str + i, 1);
                val_free(&arr); val_free(&idx);
                return result;
            }
            val_free(&arr); val_free(&idx);
            return val_null();
        }
        val_free(&arr); val_free(&idx);
        return val_null();
    }

    case NODE_OBJ_ACCESS: {
        Value obj = eval_node(it, node->as.obj_access.obj);
        if (obj.type == VAL_OBJECT) {
            Value v;
            if (node->as.obj_access.key && !node->as.obj_access.is_bracket) {
                /* Check for "length" property on objects */
                if (strcmp(node->as.obj_access.key, "length") == 0) {
                    int count = obj.as.object->count;
                    val_free(&obj);
                    return val_number(count);
                }
                if (table_get(obj.as.object, node->as.obj_access.key, &v)) {
                    Value result = val_copy(v);
                    val_free(&obj);
                    return result;
                }
            } else if (node->as.obj_access.key_expr) {
                Value key = eval_node(it, node->as.obj_access.key_expr);
                if (key.type == VAL_STRING) {
                    if (table_get(obj.as.object, key.as.string.str, &v)) {
                        Value result = val_copy(v);
                        val_free(&obj); val_free(&key);
                        return result;
                    }
                }
                val_free(&key);
            }
            val_free(&obj);
            return val_null();
        }
        /* .length on string/array */
        if (node->as.obj_access.key && strcmp(node->as.obj_access.key, "length") == 0) {
            if (obj.type == VAL_STRING) {
                int len = obj.as.string.len;
                val_free(&obj);
                return val_number(len);
            }
            if (obj.type == VAL_ARRAY) {
                int cnt = obj.as.array.count;
                val_free(&obj);
                return val_number(cnt);
            }
        }
        val_free(&obj);
        return val_null();
    }

    case NODE_FUNC_CALL: {
        char *name = node->as.func_call.name;
        int argc = node->as.func_call.arg_count;

        /* Evaluate arguments */
        Value *args = NULL;
        if (argc > 0) {
            args = malloc(sizeof(Value) * (size_t)argc);
            for (int i = 0; i < argc; i++) {
                args[i] = eval_node(it, node->as.func_call.args[i]);
            }
        }

        /* For method calls (__method_*), check class methods first */
        if (strncmp(name, "__method_", 9) == 0 && argc > 0 && args[0].type == VAL_OBJECT) {
            Value class_name_val;
            if (table_get(args[0].as.object, "__class__", &class_name_val) && class_name_val.type == VAL_STRING) {
                Value class_val;
                if (table_get(&it->classes, class_name_val.as.string.str, &class_val) && class_val.type == VAL_OBJECT) {
                    const char *method_name = name + 9;
                    Value method_val;
                    if (table_get(class_val.as.object, method_name, &method_val) && method_val.type == VAL_FUNCTION) {
                        Value *this_save = it->this_obj;
                        it->this_obj = &args[0];
                        Value result = call_function(it, method_val.as.func, args + 1, argc - 1, node->line);
                        it->this_obj = this_save;
                        for (int i = 0; i < argc; i++) val_free(&args[i]);
                        free(args);
                        return result;
                    }
                }
            }
        }

        /* Special handling for map/filter/reduce.
         * Supports both orderings:
         *   map(arr, fn)       -- arr first
         *   map("fn_name", arr) -- fn name first (Python API)
         */
        if (strcmp(name, "map") == 0 && argc >= 2) {
            Value arr, fn_ref;
            if (args[0].type == VAL_ARRAY) { arr = args[0]; fn_ref = args[1]; }
            else { arr = args[1]; fn_ref = args[0]; }

            FuncDef *fndef = NULL;
            if (fn_ref.type == VAL_FUNCTION) {
                fndef = fn_ref.as.func;
            } else if (fn_ref.type == VAL_STRING) {
                Value fn_lookup;
                if (table_get(&it->functions, fn_ref.as.string.str, &fn_lookup) && fn_lookup.type == VAL_FUNCTION)
                    fndef = fn_lookup.as.func;
            }
            if (arr.type == VAL_ARRAY && fndef) {
                Value result = val_array(arr.as.array.count);
                for (int i = 0; i < arr.as.array.count; i++) {
                    Value item = val_copy(arr.as.array.items[i]);
                    Value mapped = call_function(it, fndef, &item, 1, node->line);
                    val_array_push(&result, mapped);
                    val_free(&item);
                }
                for (int i = 0; i < argc; i++) val_free(&args[i]);
                free(args);
                return result;
            }
        }
        if (strcmp(name, "filter") == 0 && argc >= 2) {
            Value arr, fn_ref;
            if (args[0].type == VAL_ARRAY) { arr = args[0]; fn_ref = args[1]; }
            else { arr = args[1]; fn_ref = args[0]; }

            FuncDef *fndef = NULL;
            if (fn_ref.type == VAL_FUNCTION) fndef = fn_ref.as.func;
            else if (fn_ref.type == VAL_STRING) {
                Value fn_lookup;
                if (table_get(&it->functions, fn_ref.as.string.str, &fn_lookup) && fn_lookup.type == VAL_FUNCTION)
                    fndef = fn_lookup.as.func;
            }
            if (arr.type == VAL_ARRAY && fndef) {
                Value result = val_array(arr.as.array.count);
                for (int i = 0; i < arr.as.array.count; i++) {
                    Value item = val_copy(arr.as.array.items[i]);
                    Value pred = call_function(it, fndef, &item, 1, node->line);
                    if (val_is_truthy(pred)) {
                        val_array_push(&result, val_copy(arr.as.array.items[i]));
                    }
                    val_free(&item); val_free(&pred);
                }
                for (int i = 0; i < argc; i++) val_free(&args[i]);
                free(args);
                return result;
            }
        }
        if (strcmp(name, "reduce") == 0 && argc >= 3) {
            /* reduce("fn", arr, init) or reduce(arr, fn, init) */
            Value arr, fn_ref, acc;
            if (args[0].type == VAL_ARRAY) { arr = args[0]; fn_ref = args[1]; acc = val_copy(args[2]); }
            else { fn_ref = args[0]; arr = args[1]; acc = val_copy(args[2]); }

            FuncDef *fndef = NULL;
            if (fn_ref.type == VAL_FUNCTION) fndef = fn_ref.as.func;
            else if (fn_ref.type == VAL_STRING) {
                Value fn_lookup;
                if (table_get(&it->functions, fn_ref.as.string.str, &fn_lookup) && fn_lookup.type == VAL_FUNCTION)
                    fndef = fn_lookup.as.func;
            }
            if (arr.type == VAL_ARRAY && fndef) {
                for (int i = 0; i < arr.as.array.count; i++) {
                    Value call_args[2];
                    call_args[0] = acc;
                    call_args[1] = val_copy(arr.as.array.items[i]);
                    acc = call_function(it, fndef, call_args, 2, node->line);
                    val_free(&call_args[0]); val_free(&call_args[1]);
                }
                for (int i = 0; i < argc; i++) val_free(&args[i]);
                free(args);
                return acc;
            }
            val_free(&acc);
        }

        /* Check builtins */
        Value bfn;
        if (table_get(&it->builtins, name, &bfn) && bfn.type == VAL_BUILTIN) {
            Value result = bfn.as.builtin(args, argc);
            for (int i = 0; i < argc; i++) val_free(&args[i]);
            free(args);
            return result;
        }

        /* Check user-defined functions */
        Value fn_val;
        if (table_get(&it->functions, name, &fn_val) && fn_val.type == VAL_FUNCTION) {
            Value result = call_function(it, fn_val.as.func, args, argc, node->line);
            for (int i = 0; i < argc; i++) val_free(&args[i]);
            free(args);
            return result;
        }

        /* Check if it's a variable holding a function */
        Value var_val;
        if (interp_get_var(it, name, &var_val)) {
            if (var_val.type == VAL_FUNCTION) {
                Value result = call_function(it, var_val.as.func, args, argc, node->line);
                for (int i = 0; i < argc; i++) val_free(&args[i]);
                free(args);
                return result;
            }
            if (var_val.type == VAL_BUILTIN) {
                Value result = var_val.as.builtin(args, argc);
                for (int i = 0; i < argc; i++) val_free(&args[i]);
                free(args);
                return result;
            }
        }

        /* Not found */
        for (int i = 0; i < argc; i++) val_free(&args[i]);
        free(args);
        runtime_error(it, node->line, "undefined function '%s'", name);
        return val_null();
    }

    case NODE_NEW: {
        char *class_name = node->as.new_inst.class_name;
        Value class_val;
        if (!table_get(&it->classes, class_name, &class_val) || class_val.type != VAL_OBJECT) {
            runtime_error(it, node->line, "undefined class '%s'", class_name);
        }

        /* Create instance object */
        Value instance = val_object();
        table_set(instance.as.object, "__class__", val_string(class_name, (int)strlen(class_name)));

        /* Evaluate constructor args */
        int argc = node->as.new_inst.arg_count;
        Value *args = NULL;
        if (argc > 0) {
            args = malloc(sizeof(Value) * (size_t)argc);
            for (int i = 0; i < argc; i++) {
                args[i] = eval_node(it, node->as.new_inst.args[i]);
            }
        }

        /* Call constructor if exists (try "constructor" then "init") */
        Value ctor;
        if ((table_get(class_val.as.object, "constructor", &ctor) && ctor.type == VAL_FUNCTION) ||
            (table_get(class_val.as.object, "init", &ctor) && ctor.type == VAL_FUNCTION)) {
            Value *this_save = it->this_obj;
            it->this_obj = &instance;
            call_function(it, ctor.as.func, args, argc, node->line);
            it->this_obj = this_save;
        }

        for (int i = 0; i < argc; i++) val_free(&args[i]);
        free(args);
        return instance;
    }

    default:
        break;
    }

    return val_null();
}

/* ---- statement execution ---- */

static void exec_stmt(Interpreter *it, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
    case NODE_PRINT: {
        Value v = eval_node(it, node->as.print_expr);
        char *s = val_to_string(v);
        printf("%s\n", s);
        free(s);
        val_free(&v);
        break;
    }

    case NODE_ASSIGN: {
        Value v = eval_node(it, node->as.assign.value);
        /* Check if this is a 'let' (define) or reassignment */
        /* We use interp_set_var which finds existing or creates in current scope */
        interp_set_var(it, node->as.assign.name, v);
        break;
    }

    case NODE_COMPOUND_ASSIGN: {
        Value current;
        if (!interp_get_var(it, node->as.comp_assign.name, &current)) {
            runtime_error(it, node->line, "undefined variable '%s'", node->as.comp_assign.name);
        }
        Value rhs = eval_node(it, node->as.comp_assign.value);

        Value result;
        if (current.type == VAL_NUMBER && rhs.type == VAL_NUMBER) {
            switch (node->as.comp_assign.op) {
                case TOKEN_PLUS_ASSIGN:     result = val_number(current.as.number + rhs.as.number); break;
                case TOKEN_MINUS_ASSIGN:    result = val_number(current.as.number - rhs.as.number); break;
                case TOKEN_MULTIPLY_ASSIGN: result = val_number(current.as.number * rhs.as.number); break;
                case TOKEN_DIVIDE_ASSIGN:
                    if (rhs.as.number == 0) runtime_error(it, node->line, "division by zero");
                    if (current.as.number == floor(current.as.number) &&
                        rhs.as.number == floor(rhs.as.number)) {
                        result = val_number((double)((long)current.as.number / (long)rhs.as.number));
                    } else {
                        result = val_number(current.as.number / rhs.as.number);
                    }
                    break;
                default:
                    result = val_null();
            }
        } else if (node->as.comp_assign.op == TOKEN_PLUS_ASSIGN &&
                   (current.type == VAL_STRING || rhs.type == VAL_STRING)) {
            char *ls = val_to_string(current);
            char *rs = val_to_string(rhs);
            int llen = (int)strlen(ls);
            int rlen = (int)strlen(rs);
            char *out = malloc((size_t)(llen + rlen + 1));
            memcpy(out, ls, (size_t)llen);
            memcpy(out + llen, rs, (size_t)rlen);
            out[llen + rlen] = '\0';
            free(ls); free(rs);
            result = val_string_take(out, llen + rlen);
        } else {
            runtime_error(it, node->line, "unsupported types for compound assignment");
            result = val_null();
        }

        val_free(&rhs);
        interp_set_var(it, node->as.comp_assign.name, result);
        break;
    }

    case NODE_OBJ_ASSIGN: {
        Value obj = eval_node(it, node->as.obj_assign.obj);
        Value val = eval_node(it, node->as.obj_assign.value);

        if (obj.type == VAL_OBJECT) {
            if (node->as.obj_assign.is_bracket && node->as.obj_assign.key_expr) {
                Value key = eval_node(it, node->as.obj_assign.key_expr);
                if (key.type == VAL_STRING) {
                    table_set(obj.as.object, key.as.string.str, val);
                }
                val_free(&key);
            } else if (node->as.obj_assign.key) {
                table_set(obj.as.object, node->as.obj_assign.key, val);
            }
        } else if (obj.type == VAL_ARRAY && node->as.obj_assign.is_bracket && node->as.obj_assign.key_expr) {
            Value idx = eval_node(it, node->as.obj_assign.key_expr);
            if (idx.type == VAL_NUMBER) {
                int i = (int)idx.as.number;
                if (i >= 0 && i < obj.as.array.count) {
                    val_free(&obj.as.array.items[i]);
                    obj.as.array.items[i] = val;
                    val = val_null(); /* don't double-free */
                }
            }
            val_free(&idx);
        }

        /* We need to write back into the source variable if it was a variable reference */
        /* The obj already points into the table entry due to pointer semantics */
        val_free(&obj);
        break;
    }

    case NODE_IF: {
        Value cond = eval_node(it, node->as.if_stmt.condition);
        int truthy = val_is_truthy(cond);
        val_free(&cond);

        if (truthy) {
            push_scope(it);
            exec_stmts(it, node->as.if_stmt.then_body, node->as.if_stmt.then_count);
            pop_scope(it);
        } else if (node->as.if_stmt.else_body) {
            push_scope(it);
            exec_stmts(it, node->as.if_stmt.else_body, node->as.if_stmt.else_count);
            pop_scope(it);
        }
        break;
    }

    case NODE_WHILE: {
        while (1) {
            Value cond = eval_node(it, node->as.while_loop.condition);
            int truthy = val_is_truthy(cond);
            val_free(&cond);
            if (!truthy) break;

            push_scope(it);
            exec_stmts(it, node->as.while_loop.body, node->as.while_loop.body_count);
            pop_scope(it);

            if (it->break_flag) { it->break_flag = 0; break; }
            if (it->return_flag) break;
            if (it->continue_flag) { it->continue_flag = 0; }
        }
        break;
    }

    case NODE_FOR: {
        Value iterable = eval_node(it, node->as.for_loop.iterable);
        if (iterable.type == VAL_ARRAY) {
            for (int i = 0; i < iterable.as.array.count; i++) {
                push_scope(it);
                interp_def_var(it, node->as.for_loop.var, val_copy(iterable.as.array.items[i]));
                exec_stmts(it, node->as.for_loop.body, node->as.for_loop.body_count);
                pop_scope(it);

                if (it->break_flag) { it->break_flag = 0; break; }
                if (it->return_flag) break;
                if (it->continue_flag) { it->continue_flag = 0; }
            }
        } else if (iterable.type == VAL_STRING) {
            for (int i = 0; i < iterable.as.string.len; i++) {
                push_scope(it);
                interp_def_var(it, node->as.for_loop.var, val_string(iterable.as.string.str + i, 1));
                exec_stmts(it, node->as.for_loop.body, node->as.for_loop.body_count);
                pop_scope(it);

                if (it->break_flag) { it->break_flag = 0; break; }
                if (it->return_flag) break;
                if (it->continue_flag) { it->continue_flag = 0; }
            }
        } else if (iterable.type == VAL_OBJECT) {
            Value keysArr = table_keys(iterable.as.object);
            for (int i = 0; i < keysArr.as.array.count; i++) {
                push_scope(it);
                interp_def_var(it, node->as.for_loop.var, val_copy(keysArr.as.array.items[i]));
                exec_stmts(it, node->as.for_loop.body, node->as.for_loop.body_count);
                pop_scope(it);

                if (it->break_flag) { it->break_flag = 0; break; }
                if (it->return_flag) break;
                if (it->continue_flag) { it->continue_flag = 0; }
            }
            val_free(&keysArr);
        }
        val_free(&iterable);
        break;
    }

    case NODE_FUNC_DEF: {
        FuncDef *fn = malloc(sizeof(FuncDef));
        fn->name = strdup(node->as.func_def.name);
        fn->params = node->as.func_def.params;
        fn->param_count = node->as.func_def.param_count;
        fn->body = node->as.func_def.body;
        fn->body_count = node->as.func_def.body_count;
        table_set(&it->functions, fn->name, val_func(fn));
        break;
    }

    case NODE_RETURN: {
        if (node->as.return_val) {
            it->return_value = eval_node(it, node->as.return_val);
        } else {
            it->return_value = val_null();
        }
        it->return_flag = 1;
        break;
    }

    case NODE_BREAK:
        it->break_flag = 1;
        break;

    case NODE_CONTINUE:
        it->continue_flag = 1;
        break;

    case NODE_CLASS: {
        Value class_obj = val_object();
        for (int i = 0; i < node->as.class_def.method_count; i++) {
            ASTNode *method = node->as.class_def.methods[i];
            FuncDef *fn = malloc(sizeof(FuncDef));
            fn->name = strdup(method->as.func_def.name);
            fn->params = method->as.func_def.params;
            fn->param_count = method->as.func_def.param_count;
            fn->body = method->as.func_def.body;
            fn->body_count = method->as.func_def.body_count;
            table_set(class_obj.as.object, fn->name, val_func(fn));
        }
        table_set(&it->classes, node->as.class_def.name, class_obj);
        break;
    }

    case NODE_TRY_CATCH: {
        if (it->try_depth >= MAX_TRY_DEPTH) {
            runtime_error(it, node->line, "too many nested try blocks");
        }

        int saved_scope = it->scope_depth;
        it->try_depth++;
        int caught = 0;

        if (setjmp(it->try_stack[it->try_depth - 1]) == 0) {
            /* Try block */
            push_scope(it);
            exec_stmts(it, node->as.try_catch.try_body, node->as.try_catch.try_count);
            pop_scope(it);
        } else {
            /* Exception caught -- unwind scopes back to saved level */
            while (it->scope_depth > saved_scope) {
                pop_scope(it);
            }
            caught = 1;
            it->exception_active = 0;
            it->return_flag = 0;
            it->break_flag = 0;
            it->continue_flag = 0;

            push_scope(it);
            if (node->as.try_catch.catch_var && it->exception_msg) {
                int elen = (int)strlen(it->exception_msg);
                interp_def_var(it, node->as.try_catch.catch_var,
                               val_string(it->exception_msg, elen));
            }
            exec_stmts(it, node->as.try_catch.catch_body, node->as.try_catch.catch_count);
            pop_scope(it);

            free(it->exception_msg);
            it->exception_msg = NULL;
        }
        it->try_depth--;
        (void)caught;
        break;
    }

    case NODE_THROW: {
        Value v = eval_node(it, node->as.throw_val);
        char *s = val_to_string(v);
        val_free(&v);
        throw_exception(it, s);
        free(s); /* unreachable if longjmp fires */
        break;
    }

    case NODE_IMPORT: {
        const char *path = node->as.import_path;

        /* Check if already imported */
        for (int i = 0; i < it->imported_count; i++) {
            if (strcmp(it->imported[i], path) == 0) return;
        }
        if (it->imported_count >= MAX_IMPORTED) {
            runtime_error(it, node->line, "too many imports");
        }
        it->imported[it->imported_count++] = strdup(path);

        /* Read and execute file */
        FILE *f = fopen(path, "r");
        if (!f) {
            runtime_error(it, node->line, "cannot open import file '%s'", path);
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *src = malloc((size_t)size + 1);
        size_t nr = fread(src, 1, (size_t)size, f);
        src[nr] = '\0';
        fclose(f);

        interp_run(it, src);
        free(src);
        break;
    }

    default: {
        /* Expression statement -- evaluate and discard */
        Value v = eval_node(it, node);
        val_free(&v);
        break;
    }
    }
}

static void exec_stmts(Interpreter *it, ASTNode **stmts, int count) {
    for (int i = 0; i < count; i++) {
        if (it->return_flag || it->break_flag || it->continue_flag) break;
        exec_stmt(it, stmts[i]);
    }
}

/* ---- public API ---- */

void interp_init(Interpreter *it) {
    memset(it, 0, sizeof(Interpreter));
    it->scope_depth = 0;
    table_init(&it->scopes[0].vars);
    table_init(&it->globals);
    table_init(&it->functions);
    table_init(&it->builtins);
    table_init(&it->classes);
    it->this_obj = NULL;
    it->call_depth = 0;
    it->imported_count = 0;
    it->break_flag = 0;
    it->continue_flag = 0;
    it->return_flag = 0;
    it->return_value = val_null();
    it->try_depth = 0;
    it->exception_msg = NULL;
    it->exception_active = 0;
    builtins_register(it);
}

void interp_free(Interpreter *it) {
    for (int i = it->scope_depth; i >= 0; i--) {
        table_free(&it->scopes[i].vars);
    }
    table_free(&it->globals);
    table_free(&it->functions);
    table_free(&it->builtins);
    table_free(&it->classes);
    for (int i = 0; i < it->imported_count; i++) {
        free(it->imported[i]);
    }
    val_free(&it->return_value);
    free(it->exception_msg);
    it->exception_msg = NULL;
}

Value interp_eval(Interpreter *it, ASTNode *node) {
    return eval_node(it, node);
}

void interp_exec(Interpreter *it, ASTNode **stmts, int count) {
    exec_stmts(it, stmts, count);
}

void interp_run(Interpreter *it, const char *source) {
    Lexer lex;
    lexer_init(&lex, source);
    lexer_tokenize(&lex);

    Parser parser;
    parser_init(&parser, lex.tokens, lex.token_count);
    ASTNode *program = parser_parse(&parser);

    if (program && program->type == NODE_PROGRAM) {
        exec_stmts(it, program->as.program.stmts, program->as.program.count);
    }

    ast_free(program);
    lexer_free(&lex);
}
