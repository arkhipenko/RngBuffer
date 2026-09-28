//
// Host test: RngBuffer from C++
// Build and run: make -C tests
//
// 1. Lock hooks defined in a C++ file reach the C library. Before 1.1.0 the hooks were not
//    declared in the header, a C++ definition got a C++ (mangled) name, and the linker kept
//    the weak defaults. Including the header before the definitions gives them C linkage.
// 2. The header coexists with a global class RingBuffer, as declared by the nRF5 core's
//    RingBuffer.h (which Arduino.h includes there). 1.x's typedef RingBuffer clashed with it.
//

#include <stdio.h>

class RingBuffer {              // stand-in for the nRF5 core's class
public:
    int dummy;
};

#include "RngBuffer.h"

static int g_lock_calls = 0;
static int g_unlock_calls = 0;

bool rng_buffer_lock(unsigned int timeout) {
    (void) timeout;
    g_lock_calls++;
    return true;
}

void rng_buffer_unlock(void) {
    g_unlock_calls++;
}

struct Point {
    double x;
    double y;
};

int main() {
    RingBuffer core_object;     // both names usable in one file
    core_object.dummy = 0;

    Point storage[4];
    RngBuffer rb;
    Point p = { 1.5, -2.5 };
    Point q = { 0, 0 };

    int ok = rng_buffer_init_elements(&rb, storage, sizeof(Point), 4) == RNG_BUFFER_OK
          && rng_buffer_put_element(&rb, &p) == RNG_BUFFER_OK
          && rng_buffer_get_element(&rb, &q) == RNG_BUFFER_OK
          && q.x == p.x && q.y == p.y
          && core_object.dummy == 0;

    if ( ok && g_lock_calls == 2 && g_unlock_calls == 2 ) {
        printf("C++: lock override used (2 locks, 2 unlocks), element round trip ok\n");
        return 0;
    }
    printf("FAIL: ok=%d, %d locks, %d unlocks\n", ok, g_lock_calls, g_unlock_calls);
    return 1;
}
