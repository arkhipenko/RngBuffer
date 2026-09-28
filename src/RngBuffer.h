#ifndef RNG_BUFFER_H
#define RNG_BUFFER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>  // For size_t

#define RNG_BUFFER_VERSION        20001    // major * 10000 + minor * 100 + patch
#define RNG_BUFFER_VERSION_STRING "2.0.1"

#ifdef __cplusplus
extern "C" {
#endif

// Timeout passed to rng_buffer_lock(). Its unit is defined by the lock implementation.
#ifndef RNG_BUFFER_LOCK_TIMEOUT
#define RNG_BUFFER_LOCK_TIMEOUT 200
#endif

// Result of the element functions (every function with "element" in its name).
// RNG_BUFFER_OK is 0: compare with == RNG_BUFFER_OK, do not use a status as a bool.
// Values are never renumbered; new ones are appended.
typedef enum {
    RNG_BUFFER_OK          = 0,   // done
    RNG_BUFFER_FULL        = 1,   // put: no room (for all requested elements)
    RNG_BUFFER_EMPTY       = 2,   // get, peek: nothing (or not all requested elements) held
    RNG_BUFFER_LOCK_FAILED = 3,   // rng_buffer_lock() returned false; buffer unchanged
    RNG_BUFFER_INVALID     = 4    // NULL argument, not initialized, or bad init parameters
} RngBufferStatus;

// Ring Buffer structure
// The fields are private: use the functions below.
typedef struct RngBuffer {
    uint8_t* buffer;
    size_t  head;           // next write index, in elements
    size_t  tail;           // next read index, in elements
    size_t  size;           // capacity, in elements
    size_t  count;          // elements held
    size_t  element_size;   // bytes per element
    bool    is_initialized;
} RngBuffer;

// Platform lock hooks
// Every function below except init, capacity, size and element_size calls rng_buffer_lock()
// once before it touches the buffer state, and rng_buffer_unlock() once afterwards. If
// rng_buffer_lock() returns false, the function fails without touching the buffer.
// The built-in versions are weak and do nothing. That is safe only when all calls for a
// buffer come from one thread of execution. To share a buffer between an interrupt and a
// task, or between tasks, define both functions in your application:
//   - Use exactly these signatures. Overrides written for 1.x under the name ring_buffer_*
//     are no longer called: rename them.
//   - In a C++ file, include this header before the definitions (or wrap them in
//     extern "C"). Otherwise they get C++ names and the linker silently keeps the defaults.
//   - Put them in the application's own sources. A definition inside a separate static
//     library is not linked, because the weak default already satisfies the linker.
//     With PlatformIO, a library that supplies them needs lib_archive = no.
// A critical section is the recommended implementation: it needs no initialization, so
// there is no risk of the buffer being used before the lock is ready.
bool rng_buffer_lock(unsigned int timeout);
void rng_buffer_unlock(void);


// ---- Initialization (takes no lock) ----

// Initialize a buffer of capacity elements of element_size bytes each over caller storage
// of capacity * element_size bytes. The storage is zeroed. It must outlive the buffer.
// Returns: RNG_BUFFER_OK, or RNG_BUFFER_INVALID if rb or storage is NULL, element_size or
//          capacity is 0, or capacity * element_size does not fit in size_t
RngBufferStatus rng_buffer_init_elements(RngBuffer *rb, void *storage, size_t element_size, size_t capacity);

// Initialize a byte buffer over caller storage of sz bytes (element size 1)
// Returns: true if successful, false if rb or buffer is NULL or sz is 0
bool rng_buffer_init(RngBuffer *rb, uint8_t* buffer, size_t sz);


// ---- Element operations (any element size) ----
// element points to exactly element_size bytes. Elements are always copied whole.

// Add one element
// Returns: OK, FULL, LOCK_FAILED, or INVALID (rb or element NULL, or not initialized)
RngBufferStatus rng_buffer_put_element(RngBuffer *rb, const void *element);

// Remove the oldest element into *element
// Returns: OK, EMPTY, LOCK_FAILED, or INVALID (rb or element NULL, or not initialized)
RngBufferStatus rng_buffer_get_element(RngBuffer *rb, void *element);

// Read the oldest element without removing it
// Returns: same as rng_buffer_get_element()
RngBufferStatus rng_buffer_peek_element(const RngBuffer *rb, void *element);

// Add up to n elements from the array elements, as many as fit, under one lock
// *done (if done is not NULL) receives the number stored, 0 on LOCK_FAILED and INVALID.
// Returns: OK (all n stored), FULL (fewer than n stored), LOCK_FAILED, or INVALID
RngBufferStatus rng_buffer_put_elements(RngBuffer *rb, const void *elements, size_t n, size_t *done);

// Remove up to n of the oldest elements into the array elements, under one lock
// *done (if done is not NULL) receives the number removed, 0 on LOCK_FAILED and INVALID.
// Returns: OK (n removed), EMPTY (fewer than n removed), LOCK_FAILED, or INVALID
RngBufferStatus rng_buffer_get_elements(RngBuffer *rb, void *elements, size_t n, size_t *done);

// Get the number of elements held
// Returns: OK, LOCK_FAILED, or INVALID (rb or count NULL, or not initialized)
RngBufferStatus rng_buffer_count_elements(const RngBuffer *rb, size_t *count);


// ---- Byte operations (element size 1) ----

// Add a byte
// Returns: true if successful, false if the buffer is full, not initialized, not a byte
//          buffer, or the lock failed
bool rng_buffer_put(RngBuffer *rb, uint8_t data);

// Retrieve and remove the oldest byte
// Returns: true if successful, false if the buffer is empty, not initialized, not a byte
//          buffer, data is NULL, or the lock failed
bool rng_buffer_get(RngBuffer *rb, uint8_t *data);


// ---- Buffer state ----
// Queries that take the lock return the conservative value when it fails. Use
// rng_buffer_count_elements() to tell a lock failure apart.

// Remove all elements. The storage is not zeroed.
// Returns: true if successful, false if the buffer is not initialized or the lock failed
bool rng_buffer_clear(RngBuffer *rb);

// Check if the ring buffer is empty
// Returns true also if the buffer is not initialized or the lock failed
bool rng_buffer_is_empty(const RngBuffer *rb);

// Check if the ring buffer is full
// Returns true also if the buffer is not initialized or the lock failed
bool rng_buffer_is_full(const RngBuffer *rb);

// Get the current number of elements in the ring buffer
// Returns 0 if the buffer is not initialized or the lock failed
size_t rng_buffer_count(const RngBuffer *rb);

// Get the number of elements that can still be added
// Returns 0 if the buffer is not initialized or the lock failed
size_t rng_buffer_free_space(const RngBuffer *rb);

// Get the ring buffer capacity in elements (takes no lock)
// Returns 0 if the buffer is not initialized
size_t rng_buffer_capacity(const RngBuffer *rb);

// Same as rng_buffer_capacity(). Kept from 1.x; this is not the element count.
size_t rng_buffer_size(const RngBuffer *rb);

// Get the element size in bytes (takes no lock)
// Returns 0 if the buffer is not initialized
size_t rng_buffer_element_size(const RngBuffer *rb);

#ifdef __cplusplus
}
#endif

#endif  // RNG_BUFFER_H
