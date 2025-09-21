#include "fractal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double clamp01(double v) {
    if (v < 0.0) {
        return 0.0;
    }
    if (v > 1.0) {
        return 1.0;
    }
    return v;
}

static void apply_default_map(fractal_map_t *map) {
    map->r = 0.5;
    for (int i = 0; i < FA10_DIGITS; ++i) {
        map->transforms[i][0] = (double)i / 10.0;
        map->transforms[i][1] = (double)(FA10_DIGITS - i) / 10.0;
    }
}

static void parse_transforms(const char *json, fractal_map_t *map) {
    if (!json) {
        return;
    }
    for (int digit = 0; digit < FA10_DIGITS; ++digit) {
        char key[8];
        snprintf(key, sizeof(key), "\"%d\"", digit);
        const char *pos = strstr(json, key);
        if (!pos) {
            continue;
        }
        pos = strchr(pos, '[');
        if (!pos) {
            continue;
        }
        double a = map->transforms[digit][0];
        double b = map->transforms[digit][1];
        if (sscanf(pos, "[%lf,%lf]", &a, &b) == 2) {
            map->transforms[digit][0] = a;
            map->transforms[digit][1] = b;
        }
    }
    const char *rpos = strstr(json, "\"r\"");
    if (rpos) {
        double r = map->r;
        if (sscanf(rpos, "\"r\"%*[^0-9.-]%lf", &r) == 1) {
            map->r = r;
        }
    }
}

int fractal_load_map(const char *path, fractal_map_t *map) {
    if (!map) {
        return -1;
    }
    memset(map, 0, sizeof(*map));
    apply_default_map(map);
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return -1;
    }
    rewind(fp);
    map->raw_json = (char *)malloc((size_t)len + 1);
    if (!map->raw_json) {
        fclose(fp);
        return -1;
    }
    size_t read = fread(map->raw_json, 1, (size_t)len, fp);
    fclose(fp);
    map->raw_json[read] = '\0';
    map->raw_len = read;
    size_t j = 0;
    for (size_t i = 0; i < map->raw_len; ++i) {
        char c = map->raw_json[i];
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            continue;
        }
        map->raw_json[j++] = c;
    }
    map->raw_json[j] = '\0';
    map->raw_len = j;
    parse_transforms(map->raw_json, map);
    return 0;
}

void fractal_free_map(fractal_map_t *map) {
    if (!map) {
        return;
    }
    free(map->raw_json);
    map->raw_json = NULL;
    map->raw_len = 0;
}

void fractal_encode_votes(const double votes[FA10_LENGTH], char out[FA10_LENGTH + 1]) {
    for (size_t i = 0; i < FA10_LENGTH; ++i) {
        double v = clamp01(votes[i]);
        int digit = (int)llround(v * 9.0);
        if (digit < 0) {
            digit = 0;
        }
        if (digit > 9) {
            digit = 9;
        }
        out[i] = (char)('0' + digit);
    }
    out[FA10_LENGTH] = '\0';
}

void fractal_apply_sequence(const fractal_map_t *map, const char fa[FA10_LENGTH + 1], double point[2]) {
    if (!map || !fa || !point) {
        return;
    }
    point[0] = 0.0;
    point[1] = 0.0;
    for (size_t i = 0; i < FA10_LENGTH && fa[i]; ++i) {
        int digit = fa[i] - '0';
        if (digit < 0 || digit >= FA10_DIGITS) {
            continue;
        }
        point[0] = map->r * point[0] + map->transforms[digit][0];
        point[1] = map->r * point[1] + map->transforms[digit][1];
    }
}

size_t fractal_stability(const char history[][FA10_LENGTH + 1], size_t history_len, size_t window) {
    if (!history || history_len == 0) {
        return 0;
    }
    size_t count = (history_len < window) ? history_len : window;
    if (count == 0) {
        return 0;
    }
    const char *first = history[history_len - 1];
    size_t prefix = FA10_LENGTH;
    for (size_t idx = 1; idx < count; ++idx) {
        const char *cur = history[history_len - 1 - idx];
        size_t local = 0;
        while (local < FA10_LENGTH && first[local] && cur[local] && first[local] == cur[local]) {
            ++local;
        }
        if (local < prefix) {
            prefix = local;
        }
        if (prefix == 0) {
            break;
        }
    }
    return prefix;
}
