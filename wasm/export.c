#include "export.h"

#include <stdint.h>
#include <stddef.h>

#include "../core/kolibri.h"

static char *wasm_ptr(uint32_t offset) {
    return (char *)(uintptr_t)offset;
}

static const char *wasm_cptr(uint32_t offset) {
    return (const char *)(uintptr_t)offset;
}

__attribute__((export_name("kol_init")))
int wasm_kol_init(uint32_t depth, uint32_t seed) {
    return kol_init((uint8_t)depth, seed);
}

__attribute__((export_name("kol_reset")))
void wasm_kol_reset(void) {
    kol_reset();
}

__attribute__((export_name("kol_tick")))
int wasm_kol_tick(void) {
    return kol_tick();
}

__attribute__((export_name("kol_chat_push")))
int wasm_kol_chat_push(uint32_t text_ptr) {
    return kol_chat_push(wasm_cptr(text_ptr));
}

__attribute__((export_name("kol_eff")))
double wasm_kol_eff(void) {
    return kol_eff();
}

__attribute__((export_name("kol_compl")))
double wasm_kol_compl(void) {
    return kol_compl();
}

__attribute__((export_name("kol_tail_json")))
int wasm_kol_tail_json(uint32_t buf_ptr, int cap, int n) {
    return kol_tail_json(wasm_ptr(buf_ptr), cap, n);
}

__attribute__((export_name("kol_emit_digits")))
int wasm_kol_emit_digits(uint32_t buf_ptr, uint32_t max_len, uint32_t out_len_ptr) {
    size_t out_len = 0;
    int rc = kol_emit_digits((uint8_t *)wasm_ptr(buf_ptr), (size_t)max_len,
                             out_len_ptr ? &out_len : NULL);
    if (out_len_ptr) {
        uint32_t *out = (uint32_t *)wasm_ptr(out_len_ptr);
        *out = (uint32_t)out_len;
    }
    return rc;
}

__attribute__((export_name("kol_alloc")))
uint32_t wasm_kol_alloc(uint32_t size) {
    void *ptr = kol_alloc((size_t)size);
    return (uint32_t)(uintptr_t)ptr;
}

__attribute__((export_name("kol_free")))
void wasm_kol_free(uint32_t ptr) {
    kol_free((void *)(uintptr_t)ptr);
}
