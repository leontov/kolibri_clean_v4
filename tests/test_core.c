#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../core/kolibri.h"

int main(void) {
    if (kol_init(3, 2024) != 0) {
        fprintf(stderr, "kol_init failed\n");
        return 1;
    }
    kol_chat_push("Привет колибри");
    int tick_res = kol_tick();
    assert(tick_res == 0);
    uint8_t seed_digits[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    assert(kol_ingest_digits(seed_digits, sizeof(seed_digits)) == 0);
    const uint8_t bytes_payload[] = {0u, 127u, 255u, 64u};
    assert(kol_ingest_bytes(bytes_payload, sizeof(bytes_payload)) == 0);
    const float signal_payload[] = {0.0f, -1.0f, 1.0f, 0.25f, -0.25f};
    assert(kol_ingest_signal(signal_payload,
                             sizeof(signal_payload) / sizeof(signal_payload[0])) == 0);
    assert(kol_tick() == 0);
    uint8_t out_digits[128];
    size_t  out_len = 0;
    assert(kol_emit_digits(out_digits, sizeof(out_digits), &out_len) == 0);
    assert(out_len > 0);
    for (size_t i = 0; i < out_len; ++i) {
        assert(out_digits[i] <= 9);
    }
    char text_buf[256];
    int  text_len = kol_emit_text(text_buf, sizeof(text_buf));
    assert(text_len >= 0);
    assert((size_t)text_len < sizeof(text_buf));
    assert(strstr(text_buf, "Узор:") != NULL);
    assert(strstr(text_buf, "Память:") != NULL);
    double eff = kol_eff();
    double compl = kol_compl();
    assert(eff >= 0.0 && eff <= 1.0);
    assert(compl >= 0.0);
    char response[128];
    int response_len = kol_language_generate(response, sizeof(response));
    assert(response_len > 0);
    assert(strcmp(response, "Колибри пока молчит...") != 0);
    assert(strstr(response, "Колибри выделяет темы") != NULL);
    assert(strstr(response, "•") != NULL);
    assert(strstr(response, "привет") != NULL || strstr(response, "Привет") != NULL);
    char buf[2048];
    int len = kol_tail_json(buf, (int)sizeof(buf), 3);
    assert(len >= 0);
    kol_reset();
    printf("core test ok\n");
    return 0;
}
