#include "dsl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>

static Formula* fn(NodeType t){
    Formula* f=(Formula*)calloc(1,sizeof(Formula));
    f->t=t;
    f->param_index=-1;
    return f;
}

Formula* f_const(double v){ Formula* f=fn(F_CONST); f->v=v; return f; }
Formula* f_param(int idx){ Formula* f=fn(F_PARAM); f->param_index=idx; return f; }
Formula* f_x(){ return fn(F_VAR_X); }
Formula* f_add(Formula* a, Formula* b){ Formula* f=fn(F_ADD); f->a=a; f->b=b; return f; }
Formula* f_sub(Formula* a, Formula* b){ Formula* f=fn(F_SUB); f->a=a; f->b=b; return f; }
Formula* f_mul(Formula* a, Formula* b){ Formula* f=fn(F_MUL); f->a=a; f->b=b; return f; }
Formula* f_div(Formula* a, Formula* b){ Formula* f=fn(F_DIV); f->a=a; f->b=b; return f; }
Formula* f_min(Formula* a, Formula* b){ Formula* f=fn(F_MIN); f->a=a; f->b=b; return f; }
Formula* f_max(Formula* a, Formula* b){ Formula* f=fn(F_MAX); f->a=a; f->b=b; return f; }
Formula* f_sin(Formula* a){ Formula* f=fn(F_SIN); f->a=a; return f; }
Formula* f_cos(Formula* a){ Formula* f=fn(F_COS); f->a=a; return f; }
Formula* f_exp(Formula* a){ Formula* f=fn(F_EXP); f->a=a; return f; }
Formula* f_log(Formula* a){ Formula* f=fn(F_LOG); f->a=a; return f; }
Formula* f_pow(Formula* a, Formula* b){ Formula* f=fn(F_POW); f->a=a; f->b=b; return f; }
Formula* f_tanh(Formula* a){ Formula* f=fn(F_TANH); f->a=a; return f; }
Formula* f_sigmoid(Formula* a){ Formula* f=fn(F_SIGMOID); f->a=a; return f; }
Formula* f_abs(Formula* a){ Formula* f=fn(F_ABS); f->a=a; return f; }

static double safe_div(double num, double den){
    if(!isfinite(den) || fabs(den)<1e-9) return 0.0;
    return num/den;
}

static double safe_log(double v){
    double guard = fabs(v) + 1e-9;
    return log(guard);
}

static double safe_pow(double a, double b){
    double guard = fabs(a) + 1e-9;
    return pow(guard, b);
}

double f_eval(const Formula* f, const double* params, size_t param_count, double x){
    switch(f->t){
        case F_CONST: return f->v;
        case F_PARAM: return (f->param_index>=0 && (size_t)f->param_index<param_count)? params[f->param_index] : 0.0;
        case F_VAR_X: return x;
        case F_ADD: return f_eval(f->a,params,param_count,x)+f_eval(f->b,params,param_count,x);
        case F_SUB: return f_eval(f->a,params,param_count,x)-f_eval(f->b,params,param_count,x);
        case F_MUL: return f_eval(f->a,params,param_count,x)*f_eval(f->b,params,param_count,x);
        case F_DIV: return safe_div(f_eval(f->a,params,param_count,x), f_eval(f->b,params,param_count,x));
        case F_MIN: {
            double av=f_eval(f->a,params,param_count,x);
            double bv=f_eval(f->b,params,param_count,x);
            return av<bv?av:bv;
        }
        case F_MAX: {
            double av=f_eval(f->a,params,param_count,x);
            double bv=f_eval(f->b,params,param_count,x);
            return av>bv?av:bv;
        }
        case F_SIN: return sin(f_eval(f->a,params,param_count,x));
        case F_COS: return cos(f_eval(f->a,params,param_count,x));
        case F_EXP: return exp(f_eval(f->a,params,param_count,x));
        case F_LOG: return safe_log(f_eval(f->a,params,param_count,x));
        case F_POW: return safe_pow(f_eval(f->a,params,param_count,x), f_eval(f->b,params,param_count,x));
        case F_TANH: return tanh(f_eval(f->a,params,param_count,x));
        case F_SIGMOID: {
            double v=f_eval(f->a,params,param_count,x);
            double e=exp(-v);
            return 1.0/(1.0+e);
        }
        case F_ABS: return fabs(f_eval(f->a,params,param_count,x));
    }
    return 0.0;
}

static void zero_grad(double* g, size_t n){ if(g) memset(g,0,n*sizeof(double)); }

static double* new_grad_buffer(size_t n){
    if(n==0) return NULL;
    double* g=(double*)calloc(n,sizeof(double));
    return g;
}

static void add_grad(double* dst, const double* src, size_t n, double scale){
    if(!dst || !src) return;
    for(size_t i=0;i<n;i++) dst[i]+=src[i]*scale;
}

static double f_eval_grad_internal(const Formula* f, const double* params, size_t param_count,
                                   double x, double* grad_out){
    switch(f->t){
        case F_CONST:
            return f->v;
        case F_PARAM:
            if(grad_out && f->param_index>=0 && (size_t)f->param_index<param_count){
                grad_out[f->param_index]+=1.0;
            }
            return (f->param_index>=0 && (size_t)f->param_index<param_count)? params[f->param_index] : 0.0;
        case F_VAR_X:
            return x;
        case F_ADD:
        case F_SUB:
        case F_MUL:
        case F_DIV:
        case F_MIN:
        case F_MAX:
        case F_POW: {
            double* ga = grad_out?new_grad_buffer(param_count):NULL;
            double* gb = grad_out?new_grad_buffer(param_count):NULL;
            double av = f_eval_grad_internal(f->a, params, param_count, x, ga);
            double bv = (f->b)?f_eval_grad_internal(f->b, params, param_count, x, gb):0.0;
            double res=0.0;
            switch(f->t){
                case F_ADD:
                    res = av + bv;
                    if(grad_out){ add_grad(grad_out, ga, param_count, 1.0); add_grad(grad_out, gb, param_count, 1.0); }
                    break;
                case F_SUB:
                    res = av - bv;
                    if(grad_out){ add_grad(grad_out, ga, param_count, 1.0); add_grad(grad_out, gb, param_count, -1.0); }
                    break;
                case F_MUL:
                    res = av * bv;
                    if(grad_out){
                        add_grad(grad_out, ga, param_count, bv);
                        add_grad(grad_out, gb, param_count, av);
                    }
                    break;
                case F_DIV: {
                    double denom = fabs(bv)<1e-9?1e-9:bv;
                    res = av/denom;
                    if(grad_out){
                        double inv = 1.0/(denom);
                        double inv2 = inv*inv;
                        add_grad(grad_out, ga, param_count, inv);
                        add_grad(grad_out, gb, param_count, -av*inv2);
                    }
                    break;
                }
                case F_MIN:
                case F_MAX: {
                    bool take_a = (f->t==F_MIN)?(av<=bv):(av>=bv);
                    res = take_a?av:bv;
                    if(grad_out){ add_grad(grad_out, take_a?ga:gb, param_count, 1.0); }
                    break;
                }
                case F_POW: {
                    double guard = fabs(av)+1e-9;
                    double powv = pow(guard, bv);
                    double ln_guard = log(guard);
                    res = powv;
                    if(grad_out){
                        double sign = (av>=0.0)?1.0:-1.0;
                        add_grad(grad_out, ga, param_count, bv*pow(guard, bv-1.0)*sign);
                        add_grad(grad_out, gb, param_count, powv*ln_guard);
                    }
                    break;
                }
                default: break;
            }
            if(ga) free(ga);
            if(gb) free(gb);
            return res;
        }
        case F_SIN:
        case F_COS:
        case F_EXP:
        case F_LOG:
        case F_TANH:
        case F_SIGMOID:
        case F_ABS: {
            double* ga = grad_out?new_grad_buffer(param_count):NULL;
            double av = f_eval_grad_internal(f->a, params, param_count, x, ga);
            double res=0.0;
            double factor=0.0;
            switch(f->t){
                case F_SIN: res=sin(av); factor=cos(av); break;
                case F_COS: res=cos(av); factor=-sin(av); break;
                case F_EXP: res=exp(av); factor=res; break;
                case F_LOG: res=safe_log(av); factor=1.0/(fabs(av)+1e-9); break;
                case F_TANH: res=tanh(av); factor=1.0-res*res; break;
                case F_SIGMOID: {
                    res=1.0/(1.0+exp(-av));
                    factor=res*(1.0-res);
                    break;
                }
                case F_ABS: res=fabs(av); factor=(av>=0.0)?1.0:-1.0; break;
                default: break;
            }
            if(grad_out){ add_grad(grad_out, ga, param_count, factor); }
            if(ga) free(ga);
            return res;
        }
    }
    return 0.0;
}

double f_eval_grad(const Formula* f, const double* params, size_t param_count,
                   double x, double* grad_out){
    if(grad_out) zero_grad(grad_out, param_count);
    return f_eval_grad_internal(f, params, param_count, x, grad_out);
}

int f_complexity(const Formula* f){
    if(!f) return 0;
    switch(f->t){
        case F_CONST:
        case F_PARAM:
        case F_VAR_X:
            return 1;
        case F_SIN:
        case F_COS:
        case F_EXP:
        case F_LOG:
        case F_TANH:
        case F_SIGMOID:
        case F_ABS:
            return 1 + f_complexity(f->a);
        case F_MIN:
        case F_MAX:
        case F_ADD:
        case F_SUB:
        case F_MUL:
        case F_DIV:
        case F_POW:
            return 1 + f_complexity(f->a) + f_complexity(f->b);
    }
    return 0;
}

static int render_rec(const Formula* f, char* out, size_t n){
    if(n==0) return -1;
    switch(f->t){
        case F_CONST: return snprintf(out, n, "%.6g", f->v);
        case F_PARAM: return snprintf(out, n, "c%d", f->param_index);
        case F_VAR_X: return snprintf(out, n, "x");
        case F_SIN:
        case F_COS:
        case F_EXP:
        case F_LOG:
        case F_TANH:
        case F_SIGMOID:
        case F_ABS: {
            const char* name = (f->t==F_SIN)?"sin":
                               (f->t==F_COS)?"cos":
                               (f->t==F_EXP)?"exp":
                               (f->t==F_LOG)?"log":
                               (f->t==F_TANH)?"tanh":
                               (f->t==F_SIGMOID)?"sigmoid":"abs";
            int m = snprintf(out, n, "%s(", name);
            if(m<0 || (size_t)m>=n) return -1;
            int k = render_rec(f->a, out+m, n-m); if(k<0) return -1;
            int t = m+k;
            int c = snprintf(out+t, n-t, ")");
            if(c<0 || (size_t)(t+c)>=n) return -1;
            return t+c;
        }
        case F_MIN:
        case F_MAX: {
            const char* name = (f->t==F_MIN)?"min":"max";
            int m = snprintf(out, n, "%s(", name);
            if(m<0 || (size_t)m>=n) return -1;
            int k = render_rec(f->a, out+m, n-m); if(k<0) return -1; int t=m+k;
            int c = snprintf(out+t, n-t, ","); if(c<0 || (size_t)(t+c)>=n) return -1; t+=c;
            int q = render_rec(f->b, out+t, n-t); if(q<0) return -1; t+=q;
            int e = snprintf(out+t, n-t, ")"); if(e<0 || (size_t)(t+e)>=n) return -1;
            return t+e;
        }
        default: {
            const char* op = (f->t==F_ADD)?"+":
                             (f->t==F_SUB)?"-":
                             (f->t==F_MUL)?"*":
                             (f->t==F_DIV)?"/":"^";
            int m = snprintf(out, n, "("); if(m<0||(size_t)m>=n) return -1;
            int k = render_rec(f->a, out+m, n-m); if(k<0) return -1; int t=m+k;
            int c = snprintf(out+t, n-t, " %s ", op); if(c<0||(size_t)(t+c)>=n) return -1; t+=c;
            int q = render_rec(f->b, out+t, n-t); if(q<0) return -1; t+=q;
            int e = snprintf(out+t, n-t, ")"); if(e<0||(size_t)(t+e)>=n) return -1;
            return t+e;
        }
    }
}

int f_render(const Formula* f, char* out, size_t n){
    int k = render_rec(f, out, n);
    if(k<0) return -1;
    out[k]=0; return k;
}

void f_free(Formula* f){ if(!f) return; f_free(f->a); f_free(f->b); free(f); }

int f_max_param_index(const Formula* f){
    if(!f) return -1;
    int m=-1;
    if(f->t==F_PARAM && f->param_index>m) m=f->param_index;
    int la=f_max_param_index(f->a);
    int lb=f_max_param_index(f->b);
    if(la>m) m=la;
    if(lb>m) m=lb;
    return m;
}
