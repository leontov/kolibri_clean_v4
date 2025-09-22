#include "dsl.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t next_u32(uint32_t *state) {
    uint32_t x = *state;
    if (!x) {
        x = 0x12345678u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int choose(uint32_t *state, int max) {
    if (max <= 0) {
        return 0;
    }
    return (int)(next_u32(state) % (uint32_t)max);
}

static KolFormula *dsl_alloc(KolNodeType type) {
    KolFormula *f = (KolFormula *)malloc(sizeof(KolFormula));
    if (!f) {
        return NULL;
    }
    f->type = type;
    f->value = 0.0;
    f->a = NULL;
    f->b = NULL;
    f->c = NULL;
    return f;
}

KolFormula *dsl_const(double value) {
    KolFormula *f = dsl_alloc(KOL_NODE_CONST);
    if (f) {
        f->value = value;
    }
    return f;
}

KolFormula *dsl_var(void) {
    return dsl_alloc(KOL_NODE_VAR_X);
}

KolFormula *dsl_node(KolNodeType type, KolFormula *a, KolFormula *b, KolFormula *c) {
    KolFormula *f = dsl_alloc(type);
    if (!f) {
        return NULL;
    }
    f->a = a;
    f->b = b;
    f->c = c;
    return f;
}

KolFormula *dsl_clone(const KolFormula *src) {
    if (!src) {
        return NULL;
    }
    KolFormula *copy = dsl_alloc(src->type);
    if (!copy) {
        return NULL;
    }
    copy->value = src->value;
    copy->a = dsl_clone(src->a);
    copy->b = dsl_clone(src->b);
    copy->c = dsl_clone(src->c);
    return copy;
}

static KolFormula *rand_leaf(uint32_t *rng_state) {
    int choice = choose(rng_state, 2);
    if (choice == 0) {
        double val = (double)((int32_t)(next_u32(rng_state))) / (double)INT32_MAX;
        return dsl_const(val);
    }
    return dsl_var();
}

KolFormula *dsl_rand(uint32_t *rng_state, int max_depth) {
    if (max_depth <= 0) {
        return rand_leaf(rng_state);
    }
    static const KolNodeType unary_ops[] = {
        KOL_NODE_SIN, KOL_NODE_COS,  KOL_NODE_TANH, KOL_NODE_EXP,
        KOL_NODE_LOG, KOL_NODE_ABS};
    static const KolNodeType binary_ops[] = {
        KOL_NODE_ADD, KOL_NODE_SUB, KOL_NODE_MUL, KOL_NODE_DIV};
    int bucket = choose(rng_state, 3);
    if (bucket == 0) {
        KolNodeType type = unary_ops[choose(rng_state, (int)(sizeof(unary_ops) / sizeof(unary_ops[0])))];
        return dsl_node(type, dsl_rand(rng_state, max_depth - 1), NULL, NULL);
    } else if (bucket == 1) {
        KolNodeType type = binary_ops[choose(rng_state, (int)(sizeof(binary_ops) / sizeof(binary_ops[0])))];
        return dsl_node(type, dsl_rand(rng_state, max_depth - 1),
                        dsl_rand(rng_state, max_depth - 1), NULL);
    } else {
        int tern = choose(rng_state, 2);
        if (tern == 0) {
            return dsl_node(KOL_NODE_CLAMP, dsl_rand(rng_state, max_depth - 1),
                            dsl_rand(rng_state, max_depth - 1),
                            dsl_rand(rng_state, max_depth - 1));
        }
        return dsl_node(KOL_NODE_IFZ, dsl_rand(rng_state, max_depth - 1),
                        dsl_rand(rng_state, max_depth - 1),
                        dsl_rand(rng_state, max_depth - 1));
    }
}

static void collect_nodes(KolFormula *f, KolFormula **arr, size_t *idx) {
    if (!f) {
        return;
    }
    arr[(*idx)++] = f;
    collect_nodes(f->a, arr, idx);
    collect_nodes(f->b, arr, idx);
    collect_nodes(f->c, arr, idx);
}

KolFormula *dsl_mutate(const KolFormula *src, uint32_t *rng_state) {
    KolFormula *copy = dsl_clone(src);
    if (!copy) {
        return NULL;
    }
    int total = dsl_complexity(copy);
    if (total <= 0) {
        return copy;
    }
    KolFormula **nodes = (KolFormula **)malloc((size_t)total * sizeof(KolFormula *));
    if (!nodes) {
        return copy;
    }
    size_t idx = 0;
    collect_nodes(copy, nodes, &idx);
    KolFormula *target = nodes[choose(rng_state, total)];
    free(nodes);
    if (!target) {
        return copy;
    }
    int max_depth = 2 + choose(rng_state, 3);
    switch (target->type) {
    case KOL_NODE_CONST:
        target->value += ((double)(int32_t)next_u32(rng_state)) / (double)INT32_MAX;
        break;
    case KOL_NODE_VAR_X:
        target->type = KOL_NODE_CONST;
        target->value = ((double)(int32_t)next_u32(rng_state)) / (double)INT32_MAX;
        break;
    default:
        dsl_free(target->a);
        dsl_free(target->b);
        dsl_free(target->c);
        target->a = dsl_rand(rng_state, max_depth);
        target->b = dsl_rand(rng_state, max_depth - 1);
        target->c = dsl_rand(rng_state, max_depth - 1);
        break;
    }
    return copy;
}

static int is_const(const KolFormula *f) {
    return f && f->type == KOL_NODE_CONST;
}

static double safe_div(double a, double b) {
    if (fabs(b) < 1e-9) {
        return 0.0;
    }
    return a / b;
}

static double safe_log(double v) {
    if (v <= 1e-9) {
        return 0.0;
    }
    return log(v);
}

static double safe_tanh(double v) {
    double e = exp(v * 2.0);
    return (e - 1.0) / (e + 1.0);
}

static KolFormula *fold_binary(KolNodeType type, const KolFormula *a, const KolFormula *b) {
    double av = a->value;
    double bv = b->value;
    double res = 0.0;
    switch (type) {
    case KOL_NODE_ADD:
        res = av + bv;
        break;
    case KOL_NODE_SUB:
        res = av - bv;
        break;
    case KOL_NODE_MUL:
        res = av * bv;
        break;
    case KOL_NODE_DIV:
        res = safe_div(av, bv);
        break;
    default:
        break;
    }
    return dsl_const(res);
}

KolFormula *dsl_simplify(const KolFormula *src) {
    if (!src) {
        return NULL;
    }
    if (src->type == KOL_NODE_CONST || src->type == KOL_NODE_VAR_X) {
        return dsl_clone(src);
    }
    KolFormula *a = dsl_simplify(src->a);
    KolFormula *b = dsl_simplify(src->b);
    KolFormula *c = dsl_simplify(src->c);
    if (src->type == KOL_NODE_CLAMP || src->type == KOL_NODE_IFZ) {
        if (is_const(a) && is_const(b) && is_const(c)) {
            double val = src->type == KOL_NODE_CLAMP
                             ? fmax(fmin(a->value, c->value), b->value)
                             : (fabs(a->value) < 1e-6 ? b->value : c->value);
            dsl_free(a);
            dsl_free(b);
            dsl_free(c);
            return dsl_const(val);
        }
        KolFormula *node = dsl_node(src->type, a, b, c);
        if (!node) {
            dsl_free(a);
            dsl_free(b);
            dsl_free(c);
            return NULL;
        }
        node->value = src->value;
        return node;
    }
    if (src->type == KOL_NODE_ADD || src->type == KOL_NODE_SUB ||
        src->type == KOL_NODE_MUL || src->type == KOL_NODE_DIV) {
        if (is_const(a) && is_const(b)) {
            KolFormula *folded = fold_binary(src->type, a, b);
            dsl_free(a);
            dsl_free(b);
            return folded;
        }
    }
    if (src->type == KOL_NODE_SIN || src->type == KOL_NODE_COS ||
        src->type == KOL_NODE_TANH || src->type == KOL_NODE_EXP ||
        src->type == KOL_NODE_LOG || src->type == KOL_NODE_ABS) {
        if (is_const(a)) {
            double res = 0.0;
            switch (src->type) {
            case KOL_NODE_SIN:
                res = sin(a->value);
                break;
            case KOL_NODE_COS:
                res = cos(a->value);
                break;
            case KOL_NODE_TANH:
                res = safe_tanh(a->value);
                break;
            case KOL_NODE_EXP:
                res = exp(a->value);
                break;
            case KOL_NODE_LOG:
                res = safe_log(a->value);
                break;
            case KOL_NODE_ABS:
                res = fabs(a->value);
                break;
            default:
                break;
            }
            dsl_free(a);
            return dsl_const(res);
        }
    }
    KolFormula *node = dsl_node(src->type, a, b, c);
    if (!node) {
        dsl_free(a);
        dsl_free(b);
        dsl_free(c);
        return NULL;
    }
    node->value = src->value;
    return node;
}

double dsl_eval(const KolFormula *f, double x) {
    if (!f) {
        return 0.0;
    }
    switch (f->type) {
    case KOL_NODE_CONST:
        return f->value;
    case KOL_NODE_VAR_X:
        return x;
    case KOL_NODE_ADD:
        return dsl_eval(f->a, x) + dsl_eval(f->b, x);
    case KOL_NODE_SUB:
        return dsl_eval(f->a, x) - dsl_eval(f->b, x);
    case KOL_NODE_MUL:
        return dsl_eval(f->a, x) * dsl_eval(f->b, x);
    case KOL_NODE_DIV: {
        double denom = dsl_eval(f->b, x);
        if (fabs(denom) < 1e-9) {
            return 0.0;
        }
        return dsl_eval(f->a, x) / denom;
    }
    case KOL_NODE_SIN:
        return sin(dsl_eval(f->a, x));
    case KOL_NODE_COS:
        return cos(dsl_eval(f->a, x));
    case KOL_NODE_TANH:
        return tanh(dsl_eval(f->a, x));
    case KOL_NODE_EXP:
        return exp(dsl_eval(f->a, x));
    case KOL_NODE_LOG: {
        double val = dsl_eval(f->a, x);
        if (val <= 1e-9) {
            return 0.0;
        }
        return log(val);
    }
    case KOL_NODE_ABS:
        return fabs(dsl_eval(f->a, x));
    case KOL_NODE_CLAMP: {
        double val = dsl_eval(f->a, x);
        double min = dsl_eval(f->b, x);
        double max = dsl_eval(f->c, x);
        if (min > max) {
            double tmp = min;
            min = max;
            max = tmp;
        }
        if (val < min) {
            val = min;
        }
        if (val > max) {
            val = max;
        }
        return val;
    }
    case KOL_NODE_IFZ: {
        double cond = dsl_eval(f->a, x);
        if (fabs(cond) < 1e-6) {
            return dsl_eval(f->b, x);
        }
        return dsl_eval(f->c, x);
    }
    }
    return 0.0;
}

int dsl_complexity(const KolFormula *f) {
    if (!f) {
        return 0;
    }
    return 1 + dsl_complexity(f->a) + dsl_complexity(f->b) + dsl_complexity(f->c);
}

void dsl_free(KolFormula *f) {
    if (!f) {
        return;
    }
    dsl_free(f->a);
    dsl_free(f->b);
    dsl_free(f->c);
    free(f);
}

static size_t append_str(char *buf, size_t cap, size_t pos, const char *text) {
    if (pos >= cap) {
        return pos;
    }
    size_t len = strlen(text);
    size_t remain = cap - pos;
    size_t write = len < remain ? len : remain - 1;
    if (remain == 0) {
        return pos;
    }
    memcpy(buf + pos, text, write);
    pos += write;
    buf[pos] = '\0';
    return pos;
}

static size_t print_formula(const KolFormula *f, char *buf, size_t cap) {
    if (!f) {
        return append_str(buf, cap, strlen(buf), "null");
    }
    size_t pos = strlen(buf);
    switch (f->type) {
    case KOL_NODE_CONST: {
        char tmp[48];
        snprintf(tmp, sizeof(tmp), "%g", f->value);
        pos = append_str(buf, cap, pos, tmp);
        break;
    }
    case KOL_NODE_VAR_X:
        pos = append_str(buf, cap, pos, "x");
        break;
    case KOL_NODE_ADD:
        pos = append_str(buf, cap, pos, "(add ");
        print_formula(f->a, buf, cap);
        pos = append_str(buf, cap, strlen(buf), " ");
        print_formula(f->b, buf, cap);
        pos = append_str(buf, cap, strlen(buf), ")");
        break;
    case KOL_NODE_SUB:
        pos = append_str(buf, cap, pos, "(sub ");
        print_formula(f->a, buf, cap);
        pos = append_str(buf, cap, strlen(buf), " ");
        print_formula(f->b, buf, cap);
        pos = append_str(buf, cap, strlen(buf), ")");
        break;
    case KOL_NODE_MUL:
        pos = append_str(buf, cap, pos, "(mul ");
        print_formula(f->a, buf, cap);
        pos = append_str(buf, cap, strlen(buf), " ");
        print_formula(f->b, buf, cap);
        pos = append_str(buf, cap, strlen(buf), ")");
        break;
    case KOL_NODE_DIV:
        pos = append_str(buf, cap, pos, "(div ");
        print_formula(f->a, buf, cap);
        pos = append_str(buf, cap, strlen(buf), " ");
        print_formula(f->b, buf, cap);
        pos = append_str(buf, cap, strlen(buf), ")");
        break;
    case KOL_NODE_SIN:
        pos = append_str(buf, cap, pos, "(sin ");
        print_formula(f->a, buf, cap);
        pos = append_str(buf, cap, strlen(buf), ")");
        break;
    case KOL_NODE_COS:
        pos = append_str(buf, cap, pos, "(cos ");
        print_formula(f->a, buf, cap);
        pos = append_str(buf, cap, strlen(buf), ")");
        break;
    case KOL_NODE_TANH:
        pos = append_str(buf, cap, pos, "(tanh ");
        print_formula(f->a, buf, cap);
        pos = append_str(buf, cap, strlen(buf), ")");
        break;
    case KOL_NODE_EXP:
        pos = append_str(buf, cap, pos, "(exp ");
        print_formula(f->a, buf, cap);
        pos = append_str(buf, cap, strlen(buf), ")");
        break;
    case KOL_NODE_LOG:
        pos = append_str(buf, cap, pos, "(log ");
        print_formula(f->a, buf, cap);
        pos = append_str(buf, cap, strlen(buf), ")");
        break;
    case KOL_NODE_ABS:
        pos = append_str(buf, cap, pos, "(abs ");
        print_formula(f->a, buf, cap);
        pos = append_str(buf, cap, strlen(buf), ")");
        break;
    case KOL_NODE_CLAMP:
        pos = append_str(buf, cap, pos, "(clamp ");
        print_formula(f->a, buf, cap);
        pos = append_str(buf, cap, strlen(buf), " ");
        print_formula(f->b, buf, cap);
        pos = append_str(buf, cap, strlen(buf), " ");
        print_formula(f->c, buf, cap);
        pos = append_str(buf, cap, strlen(buf), ")");
        break;
    case KOL_NODE_IFZ:
        pos = append_str(buf, cap, pos, "(ifz ");
        print_formula(f->a, buf, cap);
        pos = append_str(buf, cap, strlen(buf), " ");
        print_formula(f->b, buf, cap);
        pos = append_str(buf, cap, strlen(buf), " ");
        print_formula(f->c, buf, cap);
        pos = append_str(buf, cap, strlen(buf), ")");
        break;
    }
    return pos;
}

char *dsl_print(const KolFormula *f) {
    int nodes = dsl_complexity(f);
    size_t cap = (size_t)(nodes * 32 + 16);
    char *buf = (char *)malloc(cap);
    if (!buf) {
        return NULL;
    }
    buf[0] = '\0';
    print_formula(f, buf, cap);
    return buf;
}
