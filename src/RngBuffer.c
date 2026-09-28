#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "RngBuffer.h"

/**
 * @brief Platform implementation of a locking mechanism for data integrity of the RngBuffer
 *        in the pre-emptive multitasking environment.
 *        Recommendation is to implement enter/exit critical section as this typically
 *        does not require initialization, and there is no risk of using this function
 *        before underlying locking mechanism is ready to be used.
 *        See RngBuffer.h for the rules an override must follow.
 *
 */
__attribute__((weak)) bool rng_buffer_lock(unsigned int timeout) {
    (void) timeout;
    return true;
}

/**
 * @brief Platform implementation of a locking mechanism for data integrity of the RngBuffer
 *        in the pre-emptive multitasking environment.
 *
 */
__attribute__((weak)) void rng_buffer_unlock(void) {
}


/**
 * @brief True if rb points to an initialized buffer
 */
static bool is_ready(const RngBuffer *rb) {
    return rb != NULL && rb->is_initialized;
}

/**
 * @brief Index n elements after the given one, wrapping at the end of the storage
 *        (n <= size; a compare instead of % avoids a division on MCUs without a
 *        hardware divider, and index + n cannot overflow)
 */
static size_t advance(const RngBuffer *rb, size_t index, size_t n) {
    size_t to_end = rb->size - index;
    return ( n < to_end ) ? index + n : n - to_end;
}

/**
 * @brief Append n elements (1 <= n <= free space) at head, in at most two memcpy calls
 *        Called with the lock held
 */
static void copy_in(RngBuffer *rb, const uint8_t *src, size_t n) {
    size_t es = rb->element_size;
    size_t first = rb->size - rb->head;         // elements before the end of the storage
    if ( first > n ) first = n;

    memcpy(rb->buffer + rb->head * es, src, first * es);
    if ( n > first ) memcpy(rb->buffer, src + first * es, (n - first) * es);
    rb->head = advance(rb, rb->head, n);
    rb->count += n;
}

/**
 * @brief Copy the n oldest elements (1 <= n <= count) from tail without removing them
 *        Called with the lock held
 */
static void copy_out(const RngBuffer *rb, uint8_t *dst, size_t n) {
    size_t es = rb->element_size;
    size_t first = rb->size - rb->tail;
    if ( first > n ) first = n;

    memcpy(dst, rb->buffer + rb->tail * es, first * es);
    if ( n > first ) memcpy(dst + first * es, rb->buffer, (n - first) * es);
}

/**
 * @brief Remove the n oldest elements (n <= count). Called with the lock held
 */
static void drop(RngBuffer *rb, size_t n) {
    rb->tail = advance(rb, rb->tail, n);
    rb->count -= n;
}


/**
 * @brief Initializes a ring buffer of fixed-size elements
 *        External storage of capacity * element_size bytes needs to be provided
 *
 * @param rb - pointer to the RngBuffer structure
 * @param storage - pointer to the storage
 * @param element_size - bytes per element
 * @param capacity - number of elements
 * @return RNG_BUFFER_OK - initialization successful
 * @return RNG_BUFFER_INVALID - NULL pointer, zero size, or storage size overflows size_t
 */
RngBufferStatus rng_buffer_init_elements(RngBuffer *rb, void *storage, size_t element_size, size_t capacity) {
    if ( rb == NULL ) return RNG_BUFFER_INVALID;
    rb->is_initialized = false;
    if ( storage == NULL || element_size == 0 || capacity == 0 ) return RNG_BUFFER_INVALID;
    if ( capacity > SIZE_MAX / element_size ) return RNG_BUFFER_INVALID;
    rb->buffer = (uint8_t *) storage;
    rb->head = 0;
    rb->tail = 0;
    rb->size = capacity;
    rb->count = 0;
    rb->element_size = element_size;
    memset(rb->buffer, 0, capacity * element_size);
    rb->is_initialized = true;
    return RNG_BUFFER_OK;
}

/**
 * @brief Initializes a byte ring buffer (element size 1)
 *        External storage needs to be provided
 *
 * @param rb - pointer to the RngBuffer structure
 * @param buffer - pointer to the storage of bytes
 * @param sz - size of buffer
 * @return true - initialization successful
 * @return false - initialization not successful
 */
bool rng_buffer_init(RngBuffer *rb, uint8_t* buffer, size_t sz) {
    return rng_buffer_init_elements(rb, buffer, 1, sz) == RNG_BUFFER_OK;
}

/**
 * @brief Add one element to the ring buffer
 *
 * @param rb - pointer to the RngBuffer structure
 * @param element - pointer to element_size bytes
 * @return RNG_BUFFER_OK, RNG_BUFFER_FULL, RNG_BUFFER_LOCK_FAILED or RNG_BUFFER_INVALID
 */
RngBufferStatus rng_buffer_put_element(RngBuffer *rb, const void *element) {
    if ( !is_ready(rb) || element == NULL ) return RNG_BUFFER_INVALID;
    if ( !rng_buffer_lock(RNG_BUFFER_LOCK_TIMEOUT) ) return RNG_BUFFER_LOCK_FAILED;

    RngBufferStatus status = RNG_BUFFER_FULL;
    if ( rb->count < rb->size ) {
        copy_in(rb, (const uint8_t *) element, 1);
        status = RNG_BUFFER_OK;
    }

    rng_buffer_unlock();
    return status;
}

/**
 * @brief Remove the oldest element from the ring buffer
 *
 * @param rb - pointer to the RngBuffer structure
 * @param element - pointer to element_size bytes to be updated
 * @return RNG_BUFFER_OK, RNG_BUFFER_EMPTY, RNG_BUFFER_LOCK_FAILED or RNG_BUFFER_INVALID
 */
RngBufferStatus rng_buffer_get_element(RngBuffer *rb, void *element) {
    if ( !is_ready(rb) || element == NULL ) return RNG_BUFFER_INVALID;
    if ( !rng_buffer_lock(RNG_BUFFER_LOCK_TIMEOUT) ) return RNG_BUFFER_LOCK_FAILED;

    RngBufferStatus status = RNG_BUFFER_EMPTY;
    if ( rb->count > 0 ) {
        copy_out(rb, (uint8_t *) element, 1);
        drop(rb, 1);
        status = RNG_BUFFER_OK;
    }

    rng_buffer_unlock();
    return status;
}

/**
 * @brief Read the oldest element without removing it
 *
 * @param rb - pointer to the RngBuffer structure
 * @param element - pointer to element_size bytes to be updated
 * @return RNG_BUFFER_OK, RNG_BUFFER_EMPTY, RNG_BUFFER_LOCK_FAILED or RNG_BUFFER_INVALID
 */
RngBufferStatus rng_buffer_peek_element(const RngBuffer *rb, void *element) {
    if ( !is_ready(rb) || element == NULL ) return RNG_BUFFER_INVALID;
    if ( !rng_buffer_lock(RNG_BUFFER_LOCK_TIMEOUT) ) return RNG_BUFFER_LOCK_FAILED;

    RngBufferStatus status = RNG_BUFFER_EMPTY;
    if ( rb->count > 0 ) {
        copy_out(rb, (uint8_t *) element, 1);
        status = RNG_BUFFER_OK;
    }

    rng_buffer_unlock();
    return status;
}

/**
 * @brief Add up to n elements, as many as fit, under one lock
 *
 * @param rb - pointer to the RngBuffer structure
 * @param elements - array of n elements
 * @param n - number of elements offered
 * @param done - if not NULL, receives the number of elements stored
 * @return RNG_BUFFER_OK - all n stored
 * @return RNG_BUFFER_FULL - fewer than n stored
 * @return RNG_BUFFER_LOCK_FAILED, RNG_BUFFER_INVALID - none stored
 */
RngBufferStatus rng_buffer_put_elements(RngBuffer *rb, const void *elements, size_t n, size_t *done) {
    if ( done != NULL ) *done = 0;
    if ( !is_ready(rb) || ( elements == NULL && n > 0 ) ) return RNG_BUFFER_INVALID;
    if ( n == 0 ) return RNG_BUFFER_OK;
    if ( !rng_buffer_lock(RNG_BUFFER_LOCK_TIMEOUT) ) return RNG_BUFFER_LOCK_FAILED;

    size_t room = rb->size - rb->count;
    size_t stored = ( n < room ) ? n : room;
    if ( stored > 0 ) copy_in(rb, (const uint8_t *) elements, stored);

    rng_buffer_unlock();
    if ( done != NULL ) *done = stored;
    return ( stored == n ) ? RNG_BUFFER_OK : RNG_BUFFER_FULL;
}

/**
 * @brief Remove up to n of the oldest elements under one lock
 *
 * @param rb - pointer to the RngBuffer structure
 * @param elements - array with room for n elements
 * @param n - number of elements requested
 * @param done - if not NULL, receives the number of elements removed
 * @return RNG_BUFFER_OK - n removed
 * @return RNG_BUFFER_EMPTY - fewer than n removed
 * @return RNG_BUFFER_LOCK_FAILED, RNG_BUFFER_INVALID - none removed
 */
RngBufferStatus rng_buffer_get_elements(RngBuffer *rb, void *elements, size_t n, size_t *done) {
    if ( done != NULL ) *done = 0;
    if ( !is_ready(rb) || ( elements == NULL && n > 0 ) ) return RNG_BUFFER_INVALID;
    if ( n == 0 ) return RNG_BUFFER_OK;
    if ( !rng_buffer_lock(RNG_BUFFER_LOCK_TIMEOUT) ) return RNG_BUFFER_LOCK_FAILED;

    size_t removed = ( n < rb->count ) ? n : rb->count;
    if ( removed > 0 ) {
        copy_out(rb, (uint8_t *) elements, removed);
        drop(rb, removed);
    }

    rng_buffer_unlock();
    if ( done != NULL ) *done = removed;
    return ( removed == n ) ? RNG_BUFFER_OK : RNG_BUFFER_EMPTY;
}

/**
 * @brief Get the number of elements held, reporting a lock failure
 *
 * @param rb - pointer to the RngBuffer structure
 * @param count - receives the number of elements, 0 on LOCK_FAILED and INVALID
 * @return RNG_BUFFER_OK, RNG_BUFFER_LOCK_FAILED or RNG_BUFFER_INVALID
 */
RngBufferStatus rng_buffer_count_elements(const RngBuffer *rb, size_t *count) {
    if ( count == NULL ) return RNG_BUFFER_INVALID;
    *count = 0;
    if ( !is_ready(rb) ) return RNG_BUFFER_INVALID;
    if ( !rng_buffer_lock(RNG_BUFFER_LOCK_TIMEOUT) ) return RNG_BUFFER_LOCK_FAILED;
    *count = rb->count;
    rng_buffer_unlock();
    return RNG_BUFFER_OK;
}

/**
 * @brief Add character to the ring buffer
 *
 * @param rb - pointer to the RngBuffer structure
 * @param data - data byte to be added to the RngBuffer
 * @return true - added successfully
 * @return false - failed to add - buffer full, not initialized, not a byte buffer or lock failed
 */
bool rng_buffer_put(RngBuffer *rb, uint8_t data) {
    if ( !is_ready(rb) || rb->element_size != 1 ) return false;
    if ( !rng_buffer_lock(RNG_BUFFER_LOCK_TIMEOUT) ) return false;

    if ( rb->count == rb->size ) {
        rng_buffer_unlock();
        return false;  // Buffer is full
    }

    rb->buffer[rb->head] = data;
    rb->head = advance(rb, rb->head, 1);
    rb->count++;

    rng_buffer_unlock();
    return true;
}

/**
 * @brief Retrieve character from the ring buffer
 *
 * @param rb - pointer to the RngBuffer structure
 * @param data - pointer to the byte location to be updated
 * @return true - if successfully retrieved
 * @return false - failed - buffer empty, not initialized, not a byte buffer, data is NULL
 *                 or lock failed
 */
bool rng_buffer_get(RngBuffer *rb, uint8_t *data) {
    if ( !is_ready(rb) || rb->element_size != 1 || data == NULL ) return false;
    if ( !rng_buffer_lock(RNG_BUFFER_LOCK_TIMEOUT) ) return false;

    if ( rb->count == 0 ) {
        rng_buffer_unlock();
        return false;  // Buffer is empty
    }

    *data = rb->buffer[rb->tail];
    drop(rb, 1);

    rng_buffer_unlock();
    return true;
}

/**
 * @brief Remove all elements from the ring buffer
 *        The storage is not zeroed, so the time spent under the lock does not depend
 *        on the buffer size
 *
 * @param rb - pointer to the RngBuffer structure
 * @return true - buffer cleared
 * @return false - buffer not initialized or lock failed
 */
bool rng_buffer_clear(RngBuffer *rb) {
    if ( !is_ready(rb) ) return false;
    if ( !rng_buffer_lock(RNG_BUFFER_LOCK_TIMEOUT) ) return false;

    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;

    rng_buffer_unlock();
    return true;
}

/**
 * @brief Check if ring buffer is empty
 *        The count is read under the lock, so a multi-byte count cannot be read
 *        half-updated on 8-bit MCUs
 *
 * @param rb - pointer to the RngBuffer structure
 * @return true - empty, not initialized or lock failed
 * @return false - holds at least one element
 */
bool rng_buffer_is_empty(const RngBuffer *rb) {
    size_t count;
    if ( rng_buffer_count_elements(rb, &count) != RNG_BUFFER_OK ) return true;
    return count == 0;
}

/**
 * @brief Check if the buffer is full
 *
 * @param rb - pointer to the RngBuffer structure
 * @return true - full, not initialized or lock failed
 * @return false - at least one element can be added
 */
bool rng_buffer_is_full(const RngBuffer *rb) {
    size_t count;
    if ( rng_buffer_count_elements(rb, &count) != RNG_BUFFER_OK ) return true;
    return count == rb->size;
}

/**
 * @brief Get the number of elements in the buffer
 *
 * @param rb - pointer to the RngBuffer structure
 * @return size_t - number of elements, 0 if not initialized or lock failed
 */
size_t rng_buffer_count(const RngBuffer *rb) {
    size_t count;
    rng_buffer_count_elements(rb, &count);     // count is 0 on failure
    return count;
}

/**
 * @brief Get the number of elements that can still be added
 *
 * @param rb - pointer to the RngBuffer structure
 * @return size_t - free space, 0 if not initialized or lock failed
 */
size_t rng_buffer_free_space(const RngBuffer *rb) {
    size_t count;
    if ( rng_buffer_count_elements(rb, &count) != RNG_BUFFER_OK ) return 0;
    return rb->size - count;
}

/**
 * @brief Get the buffer capacity in elements
 *        No locking is required: the capacity changes only in init
 *
 * @param rb - pointer to the RngBuffer structure
 * @return size_t - capacity, 0 if not initialized
 */
size_t rng_buffer_capacity(const RngBuffer *rb) {
    if ( !is_ready(rb) ) return 0;
    return rb->size;
}

/**
 * @brief Get the buffer capacity (same as rng_buffer_capacity)
 *
 * @param rb - pointer to the RngBuffer structure
 * @return size_t - capacity, 0 if not initialized
 */
size_t rng_buffer_size(const RngBuffer *rb) {
    return rng_buffer_capacity(rb);
}

/**
 * @brief Get the element size in bytes
 *        No locking is required: the element size changes only in init
 *
 * @param rb - pointer to the RngBuffer structure
 * @return size_t - element size, 0 if not initialized
 */
size_t rng_buffer_element_size(const RngBuffer *rb) {
    if ( !is_ready(rb) ) return 0;
    return rb->element_size;
}
