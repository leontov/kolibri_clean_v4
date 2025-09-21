#include "fractal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static double clamp01(double v){
    if(v < 0.0) return 0.0;
    if(v > 1.0) return 1.0;
    return v;
}

static void parse_string(const char* buf, const char* key, char* out, size_t n){
    if(!buf || !key || !out || n == 0){
        return;
    }
    const char* p = strstr(buf, key);
    if(!p){
        return;
    }
    const char* colon = strchr(p, ':');
    if(!colon){
        return;
    }
    p = colon + 1;
    while(*p == ' ' || *p == '\t'){
        ++p;
    }
    if(*p != '"'){
        return;
    }
    ++p;
    size_t j = 0;
    while(*p && *p != '"' && j + 1 < n){
        if(*p == '\\' && p[1]){
            ++p;
        }
        out[j++] = *p++;
    }
    out[j] = 0;
}

static void parse_double(const char* buf, const char* key, double* out){
    if(!buf || !key || !out){
        return;
    }
    const char* p = strstr(buf, key);
    if(!p){
        return;
    }
    const char* colon = strchr(p, ':');
    if(!colon){
        return;
    }
    p = colon + 1;
    *out = strtod(p, NULL);
}

bool fractal_map_load(const char* path, FractalMap* map){
    if(!map){
        return false;
    }
    map->id[0] = 0;
    map->r = 0.5;
    map->coeff_sin_a = 0.8;
    map->coeff_sin_omega = 1.5707963267948966; /* pi/2 */
    map->coeff_linear = 0.6;
    map->coeff_quadratic = 0.4;
    map->coeff_tanh = 1.0;
    map->coeff_exp_amp = 0.35;
    map->coeff_exp_gamma = 0.5;
    map->coeff_log_eps = 1e-3;
    map->coeff_mix_sin = 0.25;
    map->coeff_mix_cos = 0.2;
    map->coeff_mix_phi = 1.0471975511965976; /* 60 degrees */
    map->coeff_pow_amp = 0.2;
    map->coeff_pow_exp = 2.2;
    map->coeff_reduce = 0.85;

    if(!path){
        snprintf(map->id, sizeof(map->id), "default_v1");
        return true;
    }

    FILE* f = fopen(path, "rb");
    if(!f){
        return false;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(n <= 0){
        fclose(f);
        return false;
    }
    char* buf = (char*)malloc((size_t)n + 1);
    if(!buf){
        fclose(f);
        return false;
    }
    size_t readn = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if(readn != (size_t)n){
        free(buf);
        return false;
    }
    buf[n] = 0;

    parse_string(buf, "\"id\"", map->id, sizeof(map->id));
    parse_double(buf, "\"r\"", &map->r);
    parse_double(buf, "\"coeff_sin_a\"", &map->coeff_sin_a);
    parse_double(buf, "\"coeff_sin_omega\"", &map->coeff_sin_omega);
    parse_double(buf, "\"coeff_linear\"", &map->coeff_linear);
    parse_double(buf, "\"coeff_quadratic\"", &map->coeff_quadratic);
    parse_double(buf, "\"coeff_tanh\"", &map->coeff_tanh);
    parse_double(buf, "\"coeff_exp_amp\"", &map->coeff_exp_amp);
    parse_double(buf, "\"coeff_exp_gamma\"", &map->coeff_exp_gamma);
    parse_double(buf, "\"coeff_log_eps\"", &map->coeff_log_eps);
    parse_double(buf, "\"coeff_mix_sin\"", &map->coeff_mix_sin);
    parse_double(buf, "\"coeff_mix_cos\"", &map->coeff_mix_cos);
    parse_double(buf, "\"coeff_mix_phi\"", &map->coeff_mix_phi);
    parse_double(buf, "\"coeff_pow_amp\"", &map->coeff_pow_amp);
    parse_double(buf, "\"coeff_pow_exp\"", &map->coeff_pow_exp);
    parse_double(buf, "\"coeff_reduce\"", &map->coeff_reduce);

    free(buf);

    if(map->id[0] == 0){
        snprintf(map->id, sizeof(map->id), "default_v1");
    }
    if(map->coeff_log_eps < 1e-9){
        map->coeff_log_eps = 1e-9;
    }
    if(map->coeff_reduce <= 0.0 || map->coeff_reduce > 1.0){
        map->coeff_reduce = 0.85;
    }
    if(map->r <= 0.0){
        map->r = 0.5;
    }
    return true;
}

void fractal_address_from_votes(const double votes[10], char out[11]){
    if(!out){
        return;
    }
    for(int i = 0; i < 10; ++i){
        double v = votes ? votes[i] : 0.0;
        double scaled = round(9.0 * clamp01(v));
        if(scaled < 0.0){
            scaled = 0.0;
        }
        if(scaled > 9.0){
            scaled = 9.0;
        }
        out[i] = (char)('0' + (int)scaled);
    }
    out[10] = 0;
}

int fractal_common_prefix_len(const char addrs[][11], size_t n){
    if(!addrs || n == 0){
        return 0;
    }
    int limit = 10;
    for(int pos = 0; pos < limit; ++pos){
        char ref = addrs[0][pos];
        for(size_t idx = 1; idx < n; ++idx){
            if(addrs[idx][pos] != ref){
                return pos;
            }
        }
    }
    return limit;
}

static double level_scale(const FractalMap* map, size_t level){
    double r = map ? map->r : 0.5;
    if(r <= 0.0){
        r = 0.5;
    }
    return pow(r, (double)level);
}

static Formula* add_sine(const FractalMap* map, Formula* current, size_t level){
    double scale = level_scale(map, level);
    double amp = map->coeff_sin_a * scale;
    double omega = map->coeff_sin_omega * scale;
    Formula* arg = f_mul(f_const(omega), f_x());
    Formula* term = f_mul(f_const(amp), f_sin(arg));
    return f_add(current, term);
}

static Formula* add_linear(const FractalMap* map, Formula* current, size_t level){
    double scale = level_scale(map, level);
    double coeff = map->coeff_linear * scale;
    Formula* term = f_mul(f_const(coeff), f_x());
    return f_add(current, term);
}

static Formula* add_quadratic(const FractalMap* map, Formula* current, size_t level){
    double scale = level_scale(map, level);
    double coeff = map->coeff_quadratic * scale;
    Formula* x2 = f_mul(f_x(), f_x());
    Formula* term = f_mul(f_const(coeff), x2);
    return f_add(current, term);
}

static Formula* apply_tanh(const FractalMap* map, Formula* current, size_t level){
    double scale = level_scale(map, level);
    double kappa = map->coeff_tanh * scale;
    Formula* scaled = f_mul(f_const(kappa), current);
    return f_tanh(scaled);
}

static Formula* add_exp(const FractalMap* map, Formula* current, size_t level){
    double scale = level_scale(map, level);
    double amp = map->coeff_exp_amp * scale;
    double gamma = map->coeff_exp_gamma * scale;
    Formula* x2 = f_mul(f_x(), f_x());
    Formula* inner = f_mul(f_const(-gamma), x2);
    Formula* exp_term = f_exp(inner);
    Formula* term = f_mul(f_const(amp), exp_term);
    return f_add(current, term);
}

static Formula* apply_log(const FractalMap* map, Formula* current, size_t level){
    double scale = level_scale(map, level);
    double eps = map->coeff_log_eps * scale;
    if(eps < 1e-9){
        eps = 1e-9;
    }
    Formula* shifted = f_add(f_const(eps), f_abs(current));
    return f_log(shifted);
}

static Formula* add_harmonics(const FractalMap* map, Formula* current, size_t level){
    double scale = level_scale(map, level);
    double phi = map->coeff_mix_phi * scale;
    Formula* sin_arg = f_mul(f_const(phi), f_x());
    Formula* cos_arg = f_mul(f_const(phi), f_x());
    Formula* sin_term = f_mul(f_const(map->coeff_mix_sin * scale), f_sin(sin_arg));
    Formula* cos_term = f_mul(f_const(map->coeff_mix_cos * scale), f_cos(cos_arg));
    Formula* sum = f_add(sin_term, cos_term);
    return f_add(current, sum);
}

static Formula* add_power(const FractalMap* map, Formula* current, size_t level){
    double scale = level_scale(map, level);
    double amp = map->coeff_pow_amp * scale;
    double exponent = map->coeff_pow_exp * scale;
    if(exponent < 0.5){
        exponent = 0.5;
    }
    Formula* magnitude = f_add(f_abs(f_x()), f_const(1e-3));
    Formula* pow_term = f_pow(magnitude, f_const(exponent));
    Formula* term = f_mul(f_const(amp), pow_term);
    return f_add(current, term);
}

static Formula* apply_reduce(const FractalMap* map, Formula* current, size_t level){
    double scale = level_scale(map, level);
    double factor = map->coeff_reduce + (1.0 - map->coeff_reduce) * (1.0 - scale);
    return f_mul(f_const(factor), current);
}

Formula* fractal_build_formula(const char* fa, const FractalMap* map){
    if(!map){
        return NULL;
    }
    Formula* current = f_x();
    if(!fa){
        return current;
    }
    size_t len = strlen(fa);
    for(size_t i = 0; i < 10 && i < len; ++i){
        char ch = fa[i];
        if(ch < '0' || ch > '9'){
            continue;
        }
        int digit = ch - '0';
        switch(digit){
            case 0:
                break;
            case 1:
                current = add_sine(map, current, i);
                break;
            case 2:
                current = add_linear(map, current, i);
                break;
            case 3:
                current = add_quadratic(map, current, i);
                break;
            case 4:
                current = apply_tanh(map, current, i);
                break;
            case 5:
                current = add_exp(map, current, i);
                break;
            case 6:
                current = apply_log(map, current, i);
                break;
            case 7:
                current = add_harmonics(map, current, i);
                break;
            case 8:
                current = add_power(map, current, i);
                break;
            case 9:
                current = apply_reduce(map, current, i);
                break;
            default:
                break;
        }
    }
    return current;
}
