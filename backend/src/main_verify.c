#include "chainio.h"
#include "reason.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char** argv){
    const char* path = (argc>1)? argv[1] : "logs/chain.jsonl";
    if(chain_verify(path, stdout)) return 0;
    return 1;
}
