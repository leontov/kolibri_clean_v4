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
    int len = kol_tail_json(buf, (int)sizeof(buf), 3);
    assert(len >= 0);
    kol_reset();
    printf("core test ok\n");
    return 0;
}
