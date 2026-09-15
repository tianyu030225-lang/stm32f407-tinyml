#ifndef COMPONENT_RING_BUFFER_H
#define COMPONENT_RING_BUFFER_H

#include <stdint.h>

/*
 * The buffer is embedded in the object.  A ring object therefore needs no
 * allocator and has a fixed storage cost at compile time.
 */
#define COMPONENT_RING_BUFFER_CAPACITY (256U)

typedef enum
{
    COMPONENT_RING_BUFFER_OK = 0,
    COMPONENT_RING_BUFFER_EMPTY,
    COMPONENT_RING_BUFFER_DISCARDED,
    COMPONENT_RING_BUFFER_INVALID_ARGUMENT
} component_ring_buffer_status_t;

typedef struct
{
    uint8_t storage[COMPONENT_RING_BUFFER_CAPACITY];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t discard_count;
    volatile uint8_t overflowed;
} component_ring_buffer_t;

/* Initialize or flush a buffer.  Call only while the ISR producer is stopped. */
void component_ring_buffer_init(component_ring_buffer_t *buffer);
void component_ring_buffer_reset(component_ring_buffer_t *buffer);

/* Single-producer ISR entry point.  A full buffer discards the new byte. */
component_ring_buffer_status_t component_ring_buffer_push_isr(
    component_ring_buffer_t *buffer,
    uint8_t byte_value);

/* Single-consumer task entry point. */
component_ring_buffer_status_t component_ring_buffer_pop(
    component_ring_buffer_t *buffer,
    uint8_t *byte_value);

uint16_t component_ring_buffer_count(const component_ring_buffer_t *buffer);
uint8_t component_ring_buffer_is_overflowed(const component_ring_buffer_t *buffer);
uint32_t component_ring_buffer_discard_count(const component_ring_buffer_t *buffer);
void component_ring_buffer_clear_overflow(component_ring_buffer_t *buffer);

#endif /* COMPONENT_RING_BUFFER_H */
