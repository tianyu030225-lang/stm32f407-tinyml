#include "ring_buffer.h"

static uint16_t component_ring_buffer_next_index(uint16_t index)
{
    index++;
    if (index >= COMPONENT_RING_BUFFER_CAPACITY)
    {
        index = 0U;
    }
    return index;
}

void component_ring_buffer_init(component_ring_buffer_t *buffer)
{
    if (buffer == (component_ring_buffer_t *)0)
    {
        return;
    }

    buffer->head = 0U;
    buffer->tail = 0U;
    buffer->discard_count = 0U;
    buffer->overflowed = 0U;
}

void component_ring_buffer_reset(component_ring_buffer_t *buffer)
{
    component_ring_buffer_init(buffer);
}

component_ring_buffer_status_t component_ring_buffer_push_isr(
    component_ring_buffer_t *buffer,
    uint8_t byte_value)
{
    uint16_t head;
    uint16_t next_head;

    if (buffer == (component_ring_buffer_t *)0)
    {
        return COMPONENT_RING_BUFFER_INVALID_ARGUMENT;
    }

    head = buffer->head;
    next_head = component_ring_buffer_next_index(head);
    if (next_head == buffer->tail)
    {
        buffer->discard_count++;
        buffer->overflowed = 1U;
        return COMPONENT_RING_BUFFER_DISCARDED;
    }

    buffer->storage[head] = byte_value;
    buffer->head = next_head;
    return COMPONENT_RING_BUFFER_OK;
}

component_ring_buffer_status_t component_ring_buffer_pop(
    component_ring_buffer_t *buffer,
    uint8_t *byte_value)
{
    uint16_t tail;

    if ((buffer == (component_ring_buffer_t *)0) ||
        (byte_value == (uint8_t *)0))
    {
        return COMPONENT_RING_BUFFER_INVALID_ARGUMENT;
    }

    tail = buffer->tail;
    if (tail == buffer->head)
    {
        return COMPONENT_RING_BUFFER_EMPTY;
    }

    *byte_value = buffer->storage[tail];
    buffer->tail = component_ring_buffer_next_index(tail);
    return COMPONENT_RING_BUFFER_OK;
}

uint16_t component_ring_buffer_count(const component_ring_buffer_t *buffer)
{
    uint16_t head;
    uint16_t tail;

    if (buffer == (const component_ring_buffer_t *)0)
    {
        return 0U;
    }

    head = buffer->head;
    tail = buffer->tail;
    if (head >= tail)
    {
        return (uint16_t)(head - tail);
    }
    return (uint16_t)(COMPONENT_RING_BUFFER_CAPACITY - tail + head);
}

uint8_t component_ring_buffer_is_overflowed(const component_ring_buffer_t *buffer)
{
    if (buffer == (const component_ring_buffer_t *)0)
    {
        return 0U;
    }
    return buffer->overflowed;
}

uint32_t component_ring_buffer_discard_count(const component_ring_buffer_t *buffer)
{
    if (buffer == (const component_ring_buffer_t *)0)
    {
        return 0U;
    }
    return buffer->discard_count;
}

void component_ring_buffer_clear_overflow(component_ring_buffer_t *buffer)
{
    if (buffer == (component_ring_buffer_t *)0)
    {
        return;
    }
    buffer->overflowed = 0U;
}
