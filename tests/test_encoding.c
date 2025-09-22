#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../core/engine.h"

static void assert_digits_valid(const KolEvent *event) {
    if (!event) {
        return;
    }
    if (event->length == 0) {
        assert(event->stride == 0);
        return;
    }
    assert(event->stride != 0);
    assert(event->length % event->stride == 0);
    for (size_t i = 0; i < event->length; ++i) {
        assert(event->digits[i] <= 9);
    }
}

static void assert_events_equal(const KolEvent *a, const KolEvent *b) {
    assert(a->length == b->length);
    assert(a->stride == b->stride);
    if (a->length > 0) {
        assert(memcmp(a->digits, b->digits, a->length) == 0);
    }
}

int main(void) {
    KolEngine *engine = engine_create(2, 77u);
    assert(engine != NULL);

    KolEvent first;
    KolEvent second;
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));

    const char *phrase = "hello kolibri";
    assert(engine_ingest_text(engine, phrase, &first) == 0);
    assert(engine_ingest_text(engine, phrase, &second) == 0);
    assert_digits_valid(&first);
    assert_digits_valid(&second);
    assert_events_equal(&first, &second);
    assert(first.stride == 0 || first.stride == 4);

    KolEvent multi_a;
    KolEvent multi_b;
    memset(&multi_a, 0, sizeof(multi_a));
    memset(&multi_b, 0, sizeof(multi_b));

    const char *multi = "Привет мир 世界 مرحبا दुनिया";
    assert(engine_ingest_text(engine, multi, &multi_a) == 0);
    assert(engine_ingest_text(engine, multi, &multi_b) == 0);
    assert_digits_valid(&multi_a);
    assert_digits_valid(&multi_b);
    assert_events_equal(&multi_a, &multi_b);
    assert(multi_a.stride == 0 || multi_a.stride == 4);

    KolEvent altered;
    memset(&altered, 0, sizeof(altered));
    const char *altered_text = "Привет мир 世界 مرحبا мир";
    assert(engine_ingest_text(engine, altered_text, &altered) == 0);
    assert_digits_valid(&altered);
    /* The altered phrase should lead to a different digit signature. */
    int differ = 0;
    size_t min_len = first.length < altered.length ? first.length : altered.length;
    if (first.length != altered.length || first.stride != altered.stride) {
        differ = 1;
    } else if (min_len > 0 && memcmp(first.digits, altered.digits, min_len) != 0) {
        differ = 1;
    }
    if (!differ && multi_a.length == altered.length && altered.length > 0) {
        differ = memcmp(multi_a.digits, altered.digits, altered.length) != 0;
    }
    assert(differ);

    engine_free(engine);
    printf("encoding test ok\n");
    return 0;
}
