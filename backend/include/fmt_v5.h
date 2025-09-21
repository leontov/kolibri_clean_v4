#ifndef KOLIBRI_FMT_V5_H
#define KOLIBRI_FMT_V5_H

#include <stddef.h>

void fmt_init_locale(void);

struct ReasonBlock;

int fmt_build_json(const struct ReasonBlock *block,
                   int include_crypto,
                   const char *hash_hex,
                   const char *hmac_hex,
                   char *out,
                   size_t out_size,
                   size_t *written);

int fmt_escape_string(const char *input, char *output, size_t output_size);

int fmt_print_double(char *buf, size_t buf_size, double value);

#endif /* KOLIBRI_FMT_V5_H */
