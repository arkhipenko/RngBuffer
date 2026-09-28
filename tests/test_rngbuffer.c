//
// Host unit tests for the RngBuffer library
// Build and run: make -C tests
//
// The Makefile builds this file in several variants (C99, C11, -O2, built-in default lock,
// custom lock timeout), most of them with AddressSanitizer and UndefinedBehaviorSanitizer,
// so out-of-bounds access fails the run, not only wrong values.
//
// Unless TEST_DEFAULT_LOCK is defined, this file overrides the weak lock hooks with a fake
// that counts calls, checks that lock and unlock pair up, and can be told to fail.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "RngBuffer.h"

#ifndef TEST_EXPECTED_TIMEOUT
#define TEST_EXPECTED_TIMEOUT 200
#endif

static int g_fail = 0, g_pass = 0;

#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); \
    if (_a == _b) g_pass++; \
    else { g_fail++; printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

static uint32_t g_rng = 12345;
static uint32_t rnd(void) { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }

#ifndef TEST_DEFAULT_LOCK
// ---- Fake lock ----
static int g_lock_calls = 0, g_unlock_calls = 0, g_depth = 0, g_lock_errors = 0;
static bool g_lock_fails = false;
static unsigned int g_last_timeout = 0;

bool rng_buffer_lock(unsigned int timeout) {
    g_lock_calls++;
    g_last_timeout = timeout;
    if ( g_lock_fails ) return false;
    if ( g_depth != 0 ) g_lock_errors++;    // nested lock
    g_depth++;
    return true;
}

void rng_buffer_unlock(void) {
    g_unlock_calls++;
    if ( g_depth != 1 ) g_lock_errors++;    // unlock without a matching lock
    g_depth--;
}

static void reset_lock_stats(void) {
    g_lock_calls = g_unlock_calls = g_depth = g_lock_errors = 0;
    g_lock_fails = false;
}
#endif

// A 3-byte element: an odd size, so offsets are not powers of two
typedef struct { uint8_t b[3]; } elem3_t;

static elem3_t e3(uint32_t v) {
    elem3_t e;
    e.b[0] = (uint8_t) v; e.b[1] = (uint8_t)(v >> 8); e.b[2] = (uint8_t)(v >> 16);
    return e;
}
static uint32_t v3(elem3_t e) { return e.b[0] | ((uint32_t) e.b[1] << 8) | ((uint32_t) e.b[2] << 16); }

// ---- Tests ----

static void test_version(void) {
    int major = -1, minor = -1, patch = -1;
    CHECK_EQ(sscanf(RNG_BUFFER_VERSION_STRING, "%d.%d.%d", &major, &minor, &patch), 3);
    CHECK_EQ(RNG_BUFFER_VERSION, major * 10000 + minor * 100 + patch);
    CHECK_EQ(RNG_BUFFER_OK, 0);                         // status values are a contract
    CHECK_EQ(RNG_BUFFER_FULL, 1);
    CHECK_EQ(RNG_BUFFER_EMPTY, 2);
    CHECK_EQ(RNG_BUFFER_LOCK_FAILED, 3);
    CHECK_EQ(RNG_BUFFER_INVALID, 4);
}

static void test_init(void) {
    uint8_t st[12];
    RngBuffer rb;

    CHECK(!rng_buffer_init(NULL, st, sizeof(st)));
    CHECK_EQ(rng_buffer_init_elements(NULL, st, 1, 4), RNG_BUFFER_INVALID);

    memset(&rb, 0xA5, sizeof(rb));                      // garbage, as on the stack
    CHECK(!rng_buffer_init(&rb, NULL, sizeof(st)));
    CHECK(!rb.is_initialized);
    CHECK(!rng_buffer_init(&rb, st, 0));
    CHECK(!rb.is_initialized);
    CHECK_EQ(rng_buffer_init_elements(&rb, st, 0, 4), RNG_BUFFER_INVALID);
    CHECK_EQ(rng_buffer_init_elements(&rb, st, 3, 0), RNG_BUFFER_INVALID);
    CHECK_EQ(rng_buffer_init_elements(&rb, st, 2, SIZE_MAX / 2 + 1), RNG_BUFFER_INVALID);  // size_t overflow
    CHECK(!rb.is_initialized);

    memset(st, 0x5A, sizeof(st));
    CHECK(rng_buffer_init(&rb, st, sizeof(st)));
    for ( size_t i = 0; i < sizeof(st); i++ ) CHECK_EQ(st[i], 0);   // storage zeroed
    CHECK(rng_buffer_is_empty(&rb));
    CHECK(!rng_buffer_is_full(&rb));
    CHECK_EQ(rng_buffer_count(&rb), 0);
    CHECK_EQ(rng_buffer_free_space(&rb), sizeof(st));
    CHECK_EQ(rng_buffer_capacity(&rb), sizeof(st));
    CHECK_EQ(rng_buffer_size(&rb), sizeof(st));
    CHECK_EQ(rng_buffer_element_size(&rb), 1);

    memset(st, 0x5A, sizeof(st));
    CHECK_EQ(rng_buffer_init_elements(&rb, st, 3, 4), RNG_BUFFER_OK);
    for ( size_t i = 0; i < sizeof(st); i++ ) CHECK_EQ(st[i], 0);
    CHECK_EQ(rng_buffer_capacity(&rb), 4);
    CHECK_EQ(rng_buffer_element_size(&rb), 3);
    CHECK_EQ(rng_buffer_free_space(&rb), 4);

    // A failed re-init leaves the buffer unusable rather than half-configured
    CHECK(rng_buffer_init(&rb, st, sizeof(st)));
    CHECK(rng_buffer_put(&rb, 1));
    CHECK(!rng_buffer_init(&rb, NULL, 4));
    CHECK(!rng_buffer_put(&rb, 2));
    CHECK_EQ(rng_buffer_count(&rb), 0);
}

static void test_not_initialized(void) {
    RngBuffer rb;
    uint8_t v = 77;
    elem3_t e = e3(5);
    size_t done = 99, count = 99;
    memset(&rb, 0, sizeof(rb));                         // static-style zeroed struct, init never called

#ifndef TEST_DEFAULT_LOCK
    reset_lock_stats();
#endif
    RngBuffer *targets[2] = { NULL, &rb };
    for ( int i = 0; i < 2; i++ ) {
        RngBuffer *t = targets[i];
        CHECK(!rng_buffer_put(t, 1));
        CHECK(!rng_buffer_get(t, &v));
        CHECK(!rng_buffer_clear(t));
        CHECK(rng_buffer_is_empty(t));
        CHECK(rng_buffer_is_full(t));
        CHECK_EQ(rng_buffer_count(t), 0);
        CHECK_EQ(rng_buffer_free_space(t), 0);
        CHECK_EQ(rng_buffer_capacity(t), 0);
        CHECK_EQ(rng_buffer_size(t), 0);
        CHECK_EQ(rng_buffer_element_size(t), 0);
        CHECK_EQ(rng_buffer_put_element(t, &e), RNG_BUFFER_INVALID);
        CHECK_EQ(rng_buffer_get_element(t, &e), RNG_BUFFER_INVALID);
        CHECK_EQ(rng_buffer_peek_element(t, &e), RNG_BUFFER_INVALID);
        done = 99;
        CHECK_EQ(rng_buffer_put_elements(t, &e, 1, &done), RNG_BUFFER_INVALID);
        CHECK_EQ(done, 0);
        done = 99;
        CHECK_EQ(rng_buffer_get_elements(t, &e, 1, &done), RNG_BUFFER_INVALID);
        CHECK_EQ(done, 0);
        count = 99;
        CHECK_EQ(rng_buffer_count_elements(t, &count), RNG_BUFFER_INVALID);
        CHECK_EQ(count, 0);
    }
    CHECK_EQ(v, 77);
    CHECK_EQ(v3(e), 5);
#ifndef TEST_DEFAULT_LOCK
    CHECK_EQ(g_lock_calls, 0);                          // rejected before the lock
#endif
}

static void test_fifo_and_wrap(void) {
    uint8_t st[4];
    RngBuffer rb;
    uint8_t v;

    CHECK(rng_buffer_init(&rb, st, sizeof(st)));
    for ( int round = 0; round < 10; round++ ) {        // many wrap-arounds at every offset
        for ( int i = 0; i < 4; i++ ) CHECK(rng_buffer_put(&rb, (uint8_t)(round * 10 + i)));
        CHECK(rng_buffer_is_full(&rb));
        CHECK(!rng_buffer_is_empty(&rb));
        CHECK_EQ(rng_buffer_free_space(&rb), 0);
        CHECK(!rng_buffer_put(&rb, 99));                // full: rejected
        CHECK_EQ(rng_buffer_count(&rb), 4);
        for ( int i = 0; i < 3; i++ ) {
            CHECK(rng_buffer_get(&rb, &v));
            CHECK_EQ(v, round * 10 + i);
        }
        CHECK(rng_buffer_put(&rb, 200));
        CHECK(rng_buffer_get(&rb, &v));
        CHECK_EQ(v, round * 10 + 3);
        CHECK(rng_buffer_get(&rb, &v));
        CHECK_EQ(v, 200);
        CHECK(!rng_buffer_get(&rb, &v));                // empty: rejected
        CHECK(rng_buffer_is_empty(&rb));
    }
}

static void test_size_one(void) {
    uint8_t st[1];
    RngBuffer rb;
    uint8_t v = 0;

    CHECK(rng_buffer_init(&rb, st, 1));
    for ( int i = 0; i < 5; i++ ) {
        CHECK(rng_buffer_put(&rb, (uint8_t) i));
        CHECK(rng_buffer_is_full(&rb));
        CHECK(!rng_buffer_is_empty(&rb));
        CHECK(!rng_buffer_put(&rb, 9));
        CHECK(rng_buffer_get(&rb, &v));
        CHECK_EQ(v, i);
        CHECK(rng_buffer_is_empty(&rb));
        CHECK(!rng_buffer_is_full(&rb));
    }
}

static void test_null_arguments(void) {
    uint8_t st[4];
    RngBuffer rb;
    size_t done = 99;

    CHECK(rng_buffer_init(&rb, st, sizeof(st)));
    CHECK(rng_buffer_put(&rb, 42));
#ifndef TEST_DEFAULT_LOCK
    reset_lock_stats();
#endif
    CHECK(!rng_buffer_get(&rb, NULL));                  // was a segfault in 1.0.0
    CHECK_EQ(rng_buffer_put_element(&rb, NULL), RNG_BUFFER_INVALID);
    CHECK_EQ(rng_buffer_get_element(&rb, NULL), RNG_BUFFER_INVALID);
    CHECK_EQ(rng_buffer_peek_element(&rb, NULL), RNG_BUFFER_INVALID);
    CHECK_EQ(rng_buffer_put_elements(&rb, NULL, 2, &done), RNG_BUFFER_INVALID);
    CHECK_EQ(done, 0);
    CHECK_EQ(rng_buffer_get_elements(&rb, NULL, 2, &done), RNG_BUFFER_INVALID);
    CHECK_EQ(rng_buffer_count_elements(&rb, NULL), RNG_BUFFER_INVALID);
#ifndef TEST_DEFAULT_LOCK
    CHECK_EQ(g_lock_calls, 0);                          // rejected before the lock
#endif
    CHECK_EQ(rng_buffer_count(&rb), 1);                 // nothing removed

    // n = 0 is valid, even with a NULL array, and takes no lock
    done = 99;
    CHECK_EQ(rng_buffer_put_elements(&rb, NULL, 0, &done), RNG_BUFFER_OK);
    CHECK_EQ(done, 0);
    done = 99;
    CHECK_EQ(rng_buffer_get_elements(&rb, NULL, 0, &done), RNG_BUFFER_OK);
    CHECK_EQ(done, 0);
    CHECK_EQ(rng_buffer_count(&rb), 1);
}

static void test_byte_calls_need_byte_buffer(void) {
    uint8_t st[12];
    RngBuffer rb;
    uint8_t v = 7;
    elem3_t e = e3(0x030201);

    CHECK_EQ(rng_buffer_init_elements(&rb, st, 3, 4), RNG_BUFFER_OK);
    CHECK_EQ(rng_buffer_put_element(&rb, &e), RNG_BUFFER_OK);
#ifndef TEST_DEFAULT_LOCK
    reset_lock_stats();
#endif
    CHECK(!rng_buffer_put(&rb, 1));                     // would store a third of an element
    CHECK(!rng_buffer_get(&rb, &v));                    // would remove a third of an element
#ifndef TEST_DEFAULT_LOCK
    CHECK_EQ(g_lock_calls, 0);
#endif
    CHECK_EQ(v, 7);
    CHECK_EQ(rng_buffer_count(&rb), 1);
}

static void test_byte_buffer_through_element_calls(void) {
    uint8_t st[3];
    RngBuffer rb;
    uint8_t v = 0;
    size_t count = 0;

    CHECK(rng_buffer_init(&rb, st, sizeof(st)));
    v = 11;
    CHECK_EQ(rng_buffer_put_element(&rb, &v), RNG_BUFFER_OK);
    CHECK(rng_buffer_put(&rb, 12));
    CHECK_EQ(rng_buffer_count_elements(&rb, &count), RNG_BUFFER_OK);
    CHECK_EQ(count, 2);
    CHECK_EQ(rng_buffer_peek_element(&rb, &v), RNG_BUFFER_OK);
    CHECK_EQ(v, 11);
    CHECK(rng_buffer_get(&rb, &v));
    CHECK_EQ(v, 11);
    CHECK_EQ(rng_buffer_get_element(&rb, &v), RNG_BUFFER_OK);
    CHECK_EQ(v, 12);
    CHECK_EQ(rng_buffer_get_element(&rb, &v), RNG_BUFFER_EMPTY);
    CHECK_EQ(rng_buffer_peek_element(&rb, &v), RNG_BUFFER_EMPTY);
}

static void test_elements(void) {
    typedef struct { double x; double y; } point_t;     // RunningLinreg's use
    point_t st[5];
    RngBuffer rb;
    point_t p;

    CHECK_EQ(rng_buffer_init_elements(&rb, st, sizeof(point_t), 5), RNG_BUFFER_OK);
    for ( int round = 0; round < 7; round++ ) {         // wraps at every offset
        for ( int i = 0; i < 5; i++ ) {
            p.x = round * 100 + i; p.y = -p.x;
            CHECK_EQ(rng_buffer_put_element(&rb, &p), RNG_BUFFER_OK);
        }
        p.x = 999;
        CHECK_EQ(rng_buffer_put_element(&rb, &p), RNG_BUFFER_FULL);
        CHECK(rng_buffer_is_full(&rb));
        CHECK_EQ(rng_buffer_count(&rb), 5);
        CHECK_EQ(rng_buffer_peek_element(&rb, &p), RNG_BUFFER_OK);
        CHECK(p.x == round * 100 && p.y == -(double)(round * 100));
        for ( int i = 0; i < 2; i++ ) {                 // leave 3, so the next round starts mid-storage
            CHECK_EQ(rng_buffer_get_element(&rb, &p), RNG_BUFFER_OK);
            CHECK(p.x == round * 100 + i && p.y == -p.x);
        }
        for ( int i = 2; i < 5; i++ ) {
            CHECK_EQ(rng_buffer_get_element(&rb, &p), RNG_BUFFER_OK);
            CHECK(p.x == round * 100 + i && p.y == -p.x);
        }
        CHECK_EQ(rng_buffer_get_element(&rb, &p), RNG_BUFFER_EMPTY);
        CHECK(rng_buffer_is_empty(&rb));
        p.x = 0;                                        // shift the start by one for the next round
        CHECK_EQ(rng_buffer_put_element(&rb, &p), RNG_BUFFER_OK);
        CHECK_EQ(rng_buffer_get_element(&rb, &p), RNG_BUFFER_OK);
    }
}

static void test_multi_elements(void) {
    uint8_t st[5 * 3];
    RngBuffer rb;
    elem3_t in[8], out[8];
    size_t done = 0;

    for ( int i = 0; i < 8; i++ ) in[i] = e3(0x010000u * (uint32_t) i + 0x0203u);
    CHECK_EQ(rng_buffer_init_elements(&rb, st, 3, 5), RNG_BUFFER_OK);

    // Partial put: 5 of 8 fit
    CHECK_EQ(rng_buffer_put_elements(&rb, in, 8, &done), RNG_BUFFER_FULL);
    CHECK_EQ(done, 5);
    CHECK(rng_buffer_is_full(&rb));
    CHECK_EQ(rng_buffer_put_elements(&rb, in, 1, &done), RNG_BUFFER_FULL);
    CHECK_EQ(done, 0);

    // Get 3, so the free space wraps around the end of the storage
    CHECK_EQ(rng_buffer_get_elements(&rb, out, 3, &done), RNG_BUFFER_OK);
    CHECK_EQ(done, 3);
    for ( int i = 0; i < 3; i++ ) CHECK_EQ(v3(out[i]), v3(in[i]));

    // Put 3 across the end of the storage (two segments)
    CHECK_EQ(rng_buffer_put_elements(&rb, &in[5], 3, &done), RNG_BUFFER_OK);
    CHECK_EQ(done, 3);
    CHECK_EQ(rng_buffer_count(&rb), 5);

    // Partial get across the end of the storage: 5 held, 8 requested
    memset(out, 0, sizeof(out));
    CHECK_EQ(rng_buffer_get_elements(&rb, out, 8, &done), RNG_BUFFER_EMPTY);
    CHECK_EQ(done, 5);
    CHECK_EQ(v3(out[0]), v3(in[3]));
    CHECK_EQ(v3(out[1]), v3(in[4]));
    CHECK_EQ(v3(out[2]), v3(in[5]));
    CHECK_EQ(v3(out[3]), v3(in[6]));
    CHECK_EQ(v3(out[4]), v3(in[7]));
    CHECK_EQ(v3(out[5]), 0);                            // nothing written past what was removed

    // done may be NULL
    CHECK_EQ(rng_buffer_put_elements(&rb, in, 2, NULL), RNG_BUFFER_OK);
    CHECK_EQ(rng_buffer_get_elements(&rb, out, 3, NULL), RNG_BUFFER_EMPTY);
    CHECK(rng_buffer_is_empty(&rb));
}

static void test_clear(void) {
    uint8_t st[4];
    RngBuffer rb;
    uint8_t v;

    CHECK(rng_buffer_init(&rb, st, sizeof(st)));
    CHECK(rng_buffer_clear(&rb));                       // clearing an empty buffer is fine
    for ( int i = 0; i < 3; i++ ) CHECK(rng_buffer_put(&rb, (uint8_t) i));
    CHECK(rng_buffer_get(&rb, &v));                     // tail is now mid-storage
    CHECK(rng_buffer_clear(&rb));
    CHECK(rng_buffer_is_empty(&rb));
    CHECK_EQ(rng_buffer_count(&rb), 0);
    CHECK_EQ(rng_buffer_free_space(&rb), 4);
    CHECK(!rng_buffer_get(&rb, &v));
    for ( int i = 0; i < 4; i++ ) CHECK(rng_buffer_put(&rb, (uint8_t)(10 + i)));
    CHECK(!rng_buffer_put(&rb, 99));
    for ( int i = 0; i < 4; i++ ) {
        CHECK(rng_buffer_get(&rb, &v));
        CHECK_EQ(v, 10 + i);
    }
}

#ifndef TEST_DEFAULT_LOCK
static void test_locking(void) {
    uint8_t st[4];
    RngBuffer rb;
    uint8_t v = 0;
    size_t done = 0, count = 0;

    CHECK(rng_buffer_init(&rb, st, sizeof(st)));
    CHECK(rng_buffer_put(&rb, 1));
    CHECK(rng_buffer_put(&rb, 2));

    // Every state access takes and releases the lock exactly once
    reset_lock_stats();
    rng_buffer_put(&rb, 3);
    rng_buffer_get(&rb, &v);
    rng_buffer_is_empty(&rb);
    rng_buffer_is_full(&rb);
    rng_buffer_count(&rb);
    rng_buffer_free_space(&rb);
    rng_buffer_put_element(&rb, &v);
    rng_buffer_get_element(&rb, &v);
    rng_buffer_peek_element(&rb, &v);
    rng_buffer_put_elements(&rb, st, 2, &done);
    rng_buffer_get_elements(&rb, &v, 1, &done);
    rng_buffer_count_elements(&rb, &count);
    CHECK_EQ(g_lock_calls, 12);
    CHECK_EQ(g_unlock_calls, 12);
    CHECK_EQ(g_lock_errors, 0);
    CHECK_EQ(g_last_timeout, TEST_EXPECTED_TIMEOUT);

    // Full and empty paths release the lock too
    reset_lock_stats();
    while ( rng_buffer_put(&rb, 9) ) {}
    CHECK_EQ(rng_buffer_put_element(&rb, &v), RNG_BUFFER_FULL);
    CHECK_EQ(rng_buffer_put_elements(&rb, st, 1, &done), RNG_BUFFER_FULL);
    while ( rng_buffer_get(&rb, &v) ) {}
    CHECK_EQ(rng_buffer_get_element(&rb, &v), RNG_BUFFER_EMPTY);
    CHECK_EQ(rng_buffer_peek_element(&rb, &v), RNG_BUFFER_EMPTY);
    CHECK_EQ(rng_buffer_get_elements(&rb, &v, 1, &done), RNG_BUFFER_EMPTY);
    CHECK_EQ(g_lock_calls, g_unlock_calls);
    CHECK_EQ(g_lock_errors, 0);

    // Constant properties need no lock
    reset_lock_stats();
    rng_buffer_capacity(&rb);
    rng_buffer_size(&rb);
    rng_buffer_element_size(&rb);
    CHECK_EQ(g_lock_calls, 0);

    // A failed lock changes nothing, is reported as LOCK_FAILED by the element calls,
    // and gives the conservative value elsewhere
    CHECK(rng_buffer_put(&rb, 7));
    CHECK(rng_buffer_put(&rb, 8));
    reset_lock_stats();
    g_lock_fails = true;
    v = 0;
    CHECK(!rng_buffer_put(&rb, 1));
    CHECK(!rng_buffer_get(&rb, &v));
    CHECK(!rng_buffer_clear(&rb));
    CHECK(rng_buffer_is_empty(&rb));
    CHECK(rng_buffer_is_full(&rb));
    CHECK_EQ(rng_buffer_count(&rb), 0);
    CHECK_EQ(rng_buffer_free_space(&rb), 0);
    CHECK_EQ(rng_buffer_put_element(&rb, &v), RNG_BUFFER_LOCK_FAILED);
    CHECK_EQ(rng_buffer_get_element(&rb, &v), RNG_BUFFER_LOCK_FAILED);
    CHECK_EQ(rng_buffer_peek_element(&rb, &v), RNG_BUFFER_LOCK_FAILED);
    done = 99;
    CHECK_EQ(rng_buffer_put_elements(&rb, st, 2, &done), RNG_BUFFER_LOCK_FAILED);
    CHECK_EQ(done, 0);
    done = 99;
    CHECK_EQ(rng_buffer_get_elements(&rb, st, 2, &done), RNG_BUFFER_LOCK_FAILED);
    CHECK_EQ(done, 0);
    count = 99;
    CHECK_EQ(rng_buffer_count_elements(&rb, &count), RNG_BUFFER_LOCK_FAILED);
    CHECK_EQ(count, 0);
    CHECK_EQ(rng_buffer_capacity(&rb), 4);
    CHECK_EQ(g_unlock_calls, 0);                        // no unlock without a lock
    CHECK_EQ(v, 0);
    g_lock_fails = false;
    CHECK_EQ(rng_buffer_count(&rb), 2);                 // state intact
    CHECK(rng_buffer_get(&rb, &v));
    CHECK_EQ(v, 7);
    CHECK(rng_buffer_get(&rb, &v));
    CHECK_EQ(v, 8);
}

// DEC-0001: a lock failure in the middle of a stream of elements must not split one
static void test_lock_failure_keeps_elements_whole(void) {
    typedef struct { double x; double y; } point_t;
    point_t st[4], p;
    RngBuffer rb;

    CHECK_EQ(rng_buffer_init_elements(&rb, st, sizeof(point_t), 4), RNG_BUFFER_OK);
    for ( int i = 0; i < 20; i++ ) {
        p.x = i; p.y = 2.0 * i;
        g_lock_fails = ( i % 3 == 1 );                  // every third put fails
        RngBufferStatus s = rng_buffer_put_element(&rb, &p);
        CHECK_EQ(s, g_lock_fails ? RNG_BUFFER_LOCK_FAILED : RNG_BUFFER_OK);
        g_lock_fails = ( i % 5 == 2 );                  // some gets fail too
        s = rng_buffer_get_element(&rb, &p);
        if ( g_lock_fails ) {
            CHECK_EQ(s, RNG_BUFFER_LOCK_FAILED);
        } else if ( s == RNG_BUFFER_OK ) {
            CHECK(p.y == 2.0 * p.x);                    // never a mix of two points
        }
        g_lock_fails = false;
    }
    while ( rng_buffer_get_element(&rb, &p) == RNG_BUFFER_OK ) CHECK(p.y == 2.0 * p.x);
    CHECK_EQ(g_lock_errors, 0);                         // failed locks were never unlocked
    CHECK_EQ(g_depth, 0);
}
#endif

// Random operations checked against a simple linear model, for element sizes 1 and 3
static void test_random_against_model(size_t es) {
    static const size_t caps[] = { 1, 2, 3, 7, 64, 255, 256, 1000 };
    enum { OPS = 20000, MAXN = 9 };
    static uint8_t model[OPS * MAXN * 3];
    uint8_t *st = (uint8_t *) malloc(1000 * es);
    uint8_t in[MAXN * 3], out[MAXN * 3];
    RngBuffer rb;

    for ( size_t s = 0; s < sizeof(caps) / sizeof(caps[0]); s++ ) {
        size_t cap = caps[s];
        size_t head = 0, tail = 0;                      // model holds elements model[tail..head)
        int mismatches = 0;

        CHECK_EQ(rng_buffer_init_elements(&rb, st, es, cap), RNG_BUFFER_OK);
        for ( int op = 0; op < OPS && head + MAXN <= OPS * MAXN; op++ ) {
            uint32_t r = rnd() % 100;
            size_t held = head - tail;
            size_t n = 1 + rnd() % MAXN;
            size_t done = 0;
            for ( size_t i = 0; i < n * es; i++ ) in[i] = (uint8_t) rnd();

            if ( r < 25 ) {                             // put one
                RngBufferStatus st1 = ( es == 1 )
                    ? ( rng_buffer_put(&rb, in[0]) ? RNG_BUFFER_OK : RNG_BUFFER_FULL )
                    : rng_buffer_put_element(&rb, in);
                if ( (st1 == RNG_BUFFER_OK) != (held < cap) ) mismatches++;
                if ( st1 == RNG_BUFFER_OK ) { memcpy(&model[head * es], in, es); head++; }
            } else if ( r < 48 ) {                      // put n
                RngBufferStatus st1 = rng_buffer_put_elements(&rb, in, n, &done);
                size_t expect = ( n < cap - held ) ? n : cap - held;
                if ( done != expect ) mismatches++;
                if ( st1 != (expect == n ? RNG_BUFFER_OK : RNG_BUFFER_FULL) ) mismatches++;
                memcpy(&model[head * es], in, done * es); head += done;
            } else if ( r < 70 ) {                      // get one
                RngBufferStatus st1 = ( es == 1 )
                    ? ( rng_buffer_get(&rb, out) ? RNG_BUFFER_OK : RNG_BUFFER_EMPTY )
                    : rng_buffer_get_element(&rb, out);
                if ( (st1 == RNG_BUFFER_OK) != (held > 0) ) mismatches++;
                if ( st1 == RNG_BUFFER_OK ) {
                    if ( memcmp(out, &model[tail * es], es) != 0 ) mismatches++;
                    tail++;
                }
            } else if ( r < 92 ) {                      // get n
                RngBufferStatus st1 = rng_buffer_get_elements(&rb, out, n, &done);
                size_t expect = ( n < held ) ? n : held;
                if ( done != expect ) mismatches++;
                if ( st1 != (expect == n ? RNG_BUFFER_OK : RNG_BUFFER_EMPTY) ) mismatches++;
                if ( memcmp(out, &model[tail * es], done * es) != 0 ) mismatches++;
                tail += done;
            } else if ( r < 99 ) {                      // peek
                RngBufferStatus st1 = rng_buffer_peek_element(&rb, out);
                if ( (st1 == RNG_BUFFER_OK) != (held > 0) ) mismatches++;
                if ( st1 == RNG_BUFFER_OK && memcmp(out, &model[tail * es], es) != 0 ) mismatches++;
            } else {                                    // clear
                CHECK(rng_buffer_clear(&rb));
                tail = head;
            }
            held = head - tail;
            if ( rng_buffer_count(&rb) != held ) mismatches++;
            if ( rng_buffer_free_space(&rb) != cap - held ) mismatches++;
            if ( rng_buffer_is_empty(&rb) != (held == 0) ) mismatches++;
            if ( rng_buffer_is_full(&rb) != (held == cap) ) mismatches++;
        }
        if ( mismatches ) printf("  element size %zu, capacity %zu: %d mismatches\n", es, cap, mismatches);
        CHECK_EQ(mismatches, 0);
    }
    free(st);
}

int main(void) {
    test_version();
    test_init();
    test_not_initialized();
    test_fifo_and_wrap();
    test_size_one();
    test_null_arguments();
    test_byte_calls_need_byte_buffer();
    test_byte_buffer_through_element_calls();
    test_elements();
    test_multi_elements();
    test_clear();
#ifndef TEST_DEFAULT_LOCK
    test_locking();
    reset_lock_stats();
    test_lock_failure_keeps_elements_whole();
#endif
    test_random_against_model(1);
    test_random_against_model(3);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
