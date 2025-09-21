#include "dsl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static Formula* fn(NodeType t){ Formula* f=(Formula*)calloc(1,sizeof(Formula)); f->t=t; return f; }
Formula* f_const(double v){ Formula* f=fn(F_CONST); f->v=v; return f; }
Formula* f_x(){ return fn(F_VAR_X); }
Formula* f_add(Formula* a, Formula* b){ Formula* f=fn(F_ADD); f->a=a; f->b=b; return f; }
Formula* f_sub(Formula* a, Formula* b){ Formula* f=fn(F_SUB); f->a=a; f->b=b; return f; }
Formula* f_mul(Formula* a, Formula* b){ Formula* f=fn(F_MUL); f->a=a; f->b=b; return f; }
Formula* f_div(Formula* a, Formula* b){ Formula* f=fn(F_DIV); f->a=a; f->b=b; return f; }
Formula* f_sin(Formula* a){ Formula* f=fn(F_SIN); f->a=a; return f; }

double f_eval(const Formula* f, double x){
    switch(f->t){
        case F_CONST: return f->v;
        case F_VAR_X: return x;
        case F_ADD: return f_eval(f->a,x)+f_eval(f->b,x);
        case F_SUB: return f_eval(f->a,x)-f_eval(f->b,x);
        case F_MUL: return f_eval(f->a,x)*f_eval(f->b,x);
        case F_DIV: { double d=f_eval(f->b,x); return fabs(d)<1e-12?0.0:f_eval(f->a,x)/d; }
        case F_SIN: return sin(f_eval(f->a,x));
    }
    return 0.0;
}

int f_complexity(const Formula* f){
    if(!f) return 0;
    switch(f->t){
        case F_CONST: case F_VAR_X: return 1;
        case F_SIN: return 1 + f_complexity(f->a);
        default: return 1 + f_complexity(f->a) + f_complexity(f->b);
    }
}

static int render_rec(const Formula* f, char* out, size_t n){
    if(n==0) return -1;
    switch(f->t){
        case F_CONST: return snprintf(out, n, "%.6g", f->v);
        case F_VAR_X: return snprintf(out, n, "x");
        case F_SIN: {
            int m = snprintf(out, n, "sin(");
            if(m<0 || (size_t)m>=n) return -1;
            int k = render_rec(f->a, out+m, n-m); if(k<0) return -1;
            int t = m+k;
            int c = snprintf(out+t, n-t, ")");
            if(c<0 || (size_t)(t+c)>=n) return -1;
            return t+c;
        }
        default: {
            char op = (f->t==F_ADD)?'+':(f->t==F_SUB)?'-':(f->t==F_MUL?'*':'/');
            int m = snprintf(out, n, "("); if(m<0||(size_t)m>=n) return -1;
            int k = render_rec(f->a, out+m, n-m); if(k<0) return -1; int t=m+k;
            int c = snprintf(out+t, n-t, " %c ", op); if(c<0||(size_t)(t+c)>=n) return -1; t+=c;
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
