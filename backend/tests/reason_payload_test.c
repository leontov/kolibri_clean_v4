#include "reason.h"
#include <stdio.h>
#include <string.h>

int main(void){
    ReasonBlock b = {0};
    b.step = 42;
    b.parent = 21;
    b.seed = 7;
    b.eff = 0.25;
    b.compl = 0.75;
    strncpy(b.formula, "a+b", sizeof(b.formula)-1);
    strncpy(b.prev, "prev-hash", sizeof(b.prev)-1);
    for(int i = 0; i < 10; i++){
        b.votes[i] = 0.1 * (double)(i + 1);
    }

    char buf[1024];
    int len = rb_payload_json(&b, buf, sizeof(buf));
    if(len < 0 || (size_t)len >= sizeof(buf)){
        fprintf(stderr, "rb_payload_json failed\n");
        return 1;
    }

    fwrite(buf, 1, (size_t)len, stdout);
    return 0;
}
