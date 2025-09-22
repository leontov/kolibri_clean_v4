#include <assert.h>
#include <stdio.h>

#include "../core/kolibri.h"

int main(void) {
    if (kol_init(3, 2024) != 0) {
        fprintf(stderr, "kol_init failed\n");
        return 1;
    }
    kol_chat_push("kolibri");
    int tick_res = kol_tick();
    assert(tick_res == 0);
    double eff = kol_eff();
    double compl = kol_compl();
    assert(eff >= 0.0 && eff <= 1.0);
    assert(compl >= 0.0);
    char buf[2048];
    int len = kol_tail_json(buf, (int)sizeof(buf), 3);
    assert(len >= 0);
    kol_reset();
    printf("core test ok\n");
    return 0;
}
