#ifndef KOLIBRI_FMT_V5_H
#define KOLIBRI_FMT_V5_H

#include <stddef.h>
#include <stdio.h>

void fmt_init_locale(void);
void fmt_print_double_17g(char *buf, size_t len, double value);

struct kolibri_payload;

size_t fmt_payload_json(const struct kolibri_payload *payload, char *buffer, size_t buffer_len);
int fmt_write_block(FILE *fp, const struct kolibri_payload *payload, const char *hash_hex, const char *hmac_hex);

#endif
