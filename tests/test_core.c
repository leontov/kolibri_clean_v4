#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/kolibri.h"
#include "../core/persist.h"

int main(void) {
    remove(persist_state_path());
    if (kol_init(3, 2024) != 0) {
        fprintf(stderr, "kol_init failed\n");
        return 1;
    }
    kol_chat_push("Привет колибри");
    int tick_res = kol_tick();
    assert(tick_res == 0);
    KolPersistState snap_initial;
    assert(persist_load_state(&snap_initial) == 0);
    uint8_t seed_digits[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    assert(kol_ingest_digits(seed_digits, sizeof(seed_digits)) == 0);
    const uint8_t bytes_payload[] = {0u, 127u, 255u, 64u};
    assert(kol_ingest_bytes(bytes_payload, sizeof(bytes_payload)) == 0);
    const float signal_payload[] = {0.0f, -1.0f, 1.0f, 0.25f, -0.25f};
    assert(kol_ingest_signal(signal_payload,
                             sizeof(signal_payload) / sizeof(signal_payload[0])) == 0);
    assert(kol_tick() == 0);
    KolPersistState snap_after_seed;
    assert(persist_load_state(&snap_after_seed) == 0);
    assert(snap_after_seed.step > snap_initial.step);
    double eff_after_seed_runtime = kol_eff();
    double compl_after_seed_runtime = kol_compl();
    assert(fabs(eff_after_seed_runtime - snap_after_seed.metrics.eff) < 1e-9);
    assert(fabs(compl_after_seed_runtime - snap_after_seed.metrics.compl) < 1e-9);
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
    double eff = kol_eff();
    double compl = kol_compl();
    assert(eff >= 0.0 && eff <= 1.0);
    assert(compl >= 0.0);
    char response[128];
    int response_len = kol_language_generate(response, sizeof(response));
    assert(response_len > 0);
    assert(strcmp(response, "Колибри пока молчит...") != 0);
    assert(strstr(response, "привет") != NULL || strstr(response, "Привет") != NULL);
    char buf[2048];
    int  len = kol_tail_json(buf, (int)sizeof(buf), 3);
    assert(len >= 0);
    uint8_t rich_digits[32];
    for (size_t i = 0; i < sizeof(rich_digits) / sizeof(rich_digits[0]); ++i) {
        rich_digits[i] = 9u;
    }
    assert(kol_ingest_digits(rich_digits, sizeof(rich_digits)) == 0);
    const uint8_t high_bytes[] = {255u, 255u, 255u};
    assert(kol_ingest_bytes(high_bytes, sizeof(high_bytes)) == 0);
    const float high_signal[] = {1.0f, 0.75f, 0.5f, 1.0f};
    assert(kol_ingest_signal(high_signal, sizeof(high_signal) / sizeof(high_signal[0])) == 0);
    assert(kol_tick() == 0);
    KolPersistState snap_high;
    assert(persist_load_state(&snap_high) == 0);
    assert(snap_high.step > snap_after_seed.step);
    assert(snap_high.dataset_mean > snap_after_seed.dataset_mean);
    double eff_high_runtime = kol_eff();
    double compl_high_runtime = kol_compl();
    assert(fabs(eff_high_runtime - snap_high.metrics.eff) < 1e-9);
    assert(fabs(compl_high_runtime - snap_high.metrics.compl) < 1e-9);
    uint8_t low_digits[32];
    for (size_t i = 0; i < sizeof(low_digits) / sizeof(low_digits[0]); ++i) {
        low_digits[i] = 0u;
    }
    const uint8_t low_bytes[] = {0u, 0u, 0u};
    assert(kol_ingest_digits(low_digits, sizeof(low_digits)) == 0);
    assert(kol_ingest_bytes(low_bytes, sizeof(low_bytes)) == 0);
    const float low_signal[] = {-1.0f, -0.75f, -0.5f, -1.0f};
    assert(kol_ingest_signal(low_signal, sizeof(low_signal) / sizeof(low_signal[0])) == 0);
    assert(kol_tick() == 0);
    KolPersistState snap_low;
    assert(persist_load_state(&snap_low) == 0);
    assert(snap_low.step > snap_high.step);
    assert(snap_low.dataset_mean < snap_high.dataset_mean);
    double eff_low_runtime = kol_eff();
    double compl_low_runtime = kol_compl();
    assert(fabs(eff_low_runtime - snap_low.metrics.eff) < 1e-9);
    assert(fabs(compl_low_runtime - snap_low.metrics.compl) < 1e-9);
    kol_reset();
    printf("core test ok\n");
    return 0;
}
