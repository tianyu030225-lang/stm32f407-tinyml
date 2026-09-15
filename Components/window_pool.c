#include "window_pool.h"

static uint8_t component_window_valid_id(component_window_id_t buffer_id)
{
    return (uint8_t)(buffer_id < COMPONENT_WINDOW_POOL_COUNT);
}

static void component_window_zero(component_window_buffer_t *buffer)
{
    uint16_t sample_index;
    uint8_t axis_index;

    for (sample_index = 0U;
         sample_index < COMPONENT_WINDOW_SAMPLE_COUNT;
         sample_index++)
    {
        buffer->sample_ticks[sample_index] = 0U;
        for (axis_index = 0U;
             axis_index < COMPONENT_WINDOW_AXIS_COUNT;
             axis_index++)
        {
            buffer->samples[sample_index][axis_index] = 0;
        }
    }
    buffer->metadata.sequence = 0U;
    buffer->metadata.timestamp_tick = 0U;
    buffer->metadata.session_id = 0U;
    buffer->metadata.sample_count = 0U;
    buffer->metadata.label = APP_LABEL_NONE;
    buffer->metadata.reserved = 0U;
}

void component_window_pool_init(component_window_pool_t *pool)
{
    component_window_id_t buffer_id;

    if (pool == (component_window_pool_t *)0)
    {
        return;
    }

    for (buffer_id = 0U;
         buffer_id < COMPONENT_WINDOW_POOL_COUNT;
         buffer_id++)
    {
        component_window_zero(&pool->buffers[buffer_id]);
        pool->buffers[buffer_id].state = COMPONENT_WINDOW_STATE_FREE;
    }
}

component_window_status_t component_window_begin_fill(
    component_window_pool_t *pool,
    component_window_id_t buffer_id)
{
    if (pool == (component_window_pool_t *)0)
    {
        return COMPONENT_WINDOW_INVALID_ARGUMENT;
    }
    if (!component_window_valid_id(buffer_id))
    {
        return COMPONENT_WINDOW_INVALID_ID;
    }
    if (pool->buffers[buffer_id].state != COMPONENT_WINDOW_STATE_FREE)
    {
        return COMPONENT_WINDOW_NOT_OWNER;
    }

    pool->buffers[buffer_id].state = COMPONENT_WINDOW_STATE_FILLING;
    return COMPONENT_WINDOW_OK;
}

component_window_status_t component_window_commit_ready(
    component_window_pool_t *pool,
    component_window_id_t buffer_id,
    const component_window_metadata_t *metadata)
{
    if (pool == (component_window_pool_t *)0)
    {
        return COMPONENT_WINDOW_INVALID_ARGUMENT;
    }
    if (!component_window_valid_id(buffer_id))
    {
        return COMPONENT_WINDOW_INVALID_ID;
    }
    if (pool->buffers[buffer_id].state != COMPONENT_WINDOW_STATE_FILLING)
    {
        return COMPONENT_WINDOW_NOT_OWNER;
    }
    if (metadata != (const component_window_metadata_t *)0)
    {
        pool->buffers[buffer_id].metadata = *metadata;
    }
    pool->buffers[buffer_id].state = COMPONENT_WINDOW_STATE_READY;
    return COMPONENT_WINDOW_OK;
}

component_window_status_t component_window_begin_processing(
    component_window_pool_t *pool,
    component_window_id_t buffer_id)
{
    if (pool == (component_window_pool_t *)0)
    {
        return COMPONENT_WINDOW_INVALID_ARGUMENT;
    }
    if (!component_window_valid_id(buffer_id))
    {
        return COMPONENT_WINDOW_INVALID_ID;
    }
    if (pool->buffers[buffer_id].state != COMPONENT_WINDOW_STATE_READY)
    {
        return COMPONENT_WINDOW_NOT_OWNER;
    }
    pool->buffers[buffer_id].state = COMPONENT_WINDOW_STATE_PROCESSING;
    return COMPONENT_WINDOW_OK;
}

component_window_status_t component_window_release_free(
    component_window_pool_t *pool,
    component_window_id_t buffer_id)
{
    if (pool == (component_window_pool_t *)0)
    {
        return COMPONENT_WINDOW_INVALID_ARGUMENT;
    }
    if (!component_window_valid_id(buffer_id))
    {
        return COMPONENT_WINDOW_INVALID_ID;
    }
    if (pool->buffers[buffer_id].state != COMPONENT_WINDOW_STATE_PROCESSING)
    {
        return COMPONENT_WINDOW_NOT_OWNER;
    }
    component_window_zero(&pool->buffers[buffer_id]);
    pool->buffers[buffer_id].state = COMPONENT_WINDOW_STATE_FREE;
    return COMPONENT_WINDOW_OK;
}

component_window_status_t component_window_abort_fill(
    component_window_pool_t *pool,
    component_window_id_t buffer_id)
{
    if (pool == (component_window_pool_t *)0)
    {
        return COMPONENT_WINDOW_INVALID_ARGUMENT;
    }
    if (!component_window_valid_id(buffer_id))
    {
        return COMPONENT_WINDOW_INVALID_ID;
    }
    if (pool->buffers[buffer_id].state != COMPONENT_WINDOW_STATE_FILLING)
    {
        return COMPONENT_WINDOW_NOT_OWNER;
    }
    component_window_zero(&pool->buffers[buffer_id]);
    pool->buffers[buffer_id].state = COMPONENT_WINDOW_STATE_FREE;
    return COMPONENT_WINDOW_OK;
}

component_window_status_t component_window_abort_ready(
    component_window_pool_t *pool,
    component_window_id_t buffer_id)
{
    if (pool == (component_window_pool_t *)0)
    {
        return COMPONENT_WINDOW_INVALID_ARGUMENT;
    }
    if (!component_window_valid_id(buffer_id))
    {
        return COMPONENT_WINDOW_INVALID_ID;
    }
    if (pool->buffers[buffer_id].state != COMPONENT_WINDOW_STATE_READY)
    {
        return COMPONENT_WINDOW_NOT_OWNER;
    }
    component_window_zero(&pool->buffers[buffer_id]);
    pool->buffers[buffer_id].state = COMPONENT_WINDOW_STATE_FREE;
    return COMPONENT_WINDOW_OK;
}

int16_t *component_window_write_data(
    component_window_pool_t *pool,
    component_window_id_t buffer_id)
{
    if ((pool == (component_window_pool_t *)0) ||
        !component_window_valid_id(buffer_id) ||
        (pool->buffers[buffer_id].state != COMPONENT_WINDOW_STATE_FILLING))
    {
        return (int16_t *)0;
    }
    return &pool->buffers[buffer_id].samples[0][0];
}

const int16_t *component_window_read_data(
    const component_window_pool_t *pool,
    component_window_id_t buffer_id)
{
    if ((pool == (const component_window_pool_t *)0) ||
        !component_window_valid_id(buffer_id) ||
        (pool->buffers[buffer_id].state != COMPONENT_WINDOW_STATE_PROCESSING))
    {
        return (const int16_t *)0;
    }
    return &pool->buffers[buffer_id].samples[0][0];
}

uint32_t *component_window_write_ticks(
    component_window_pool_t *pool,
    component_window_id_t buffer_id)
{
    if ((pool == (component_window_pool_t *)0) ||
        !component_window_valid_id(buffer_id) ||
        (pool->buffers[buffer_id].state != COMPONENT_WINDOW_STATE_FILLING))
    {
        return (uint32_t *)0;
    }
    return &pool->buffers[buffer_id].sample_ticks[0];
}

const uint32_t *component_window_read_ticks(
    const component_window_pool_t *pool,
    component_window_id_t buffer_id)
{
    if ((pool == (const component_window_pool_t *)0) ||
        !component_window_valid_id(buffer_id) ||
        (pool->buffers[buffer_id].state != COMPONENT_WINDOW_STATE_PROCESSING))
    {
        return (const uint32_t *)0;
    }
    return &pool->buffers[buffer_id].sample_ticks[0];
}

component_window_metadata_t *component_window_write_metadata(
    component_window_pool_t *pool,
    component_window_id_t buffer_id)
{
    if ((pool == (component_window_pool_t *)0) ||
        !component_window_valid_id(buffer_id) ||
        (pool->buffers[buffer_id].state != COMPONENT_WINDOW_STATE_FILLING))
    {
        return (component_window_metadata_t *)0;
    }
    return &pool->buffers[buffer_id].metadata;
}

const component_window_metadata_t *component_window_read_metadata(
    const component_window_pool_t *pool,
    component_window_id_t buffer_id)
{
    if ((pool == (const component_window_pool_t *)0) ||
        !component_window_valid_id(buffer_id) ||
        ((pool->buffers[buffer_id].state != COMPONENT_WINDOW_STATE_FILLING) &&
         (pool->buffers[buffer_id].state != COMPONENT_WINDOW_STATE_PROCESSING)))
    {
        return (const component_window_metadata_t *)0;
    }
    /* The producer reads its own metadata before committing FILLING -> READY;
     * the consumer reads it after taking ownership in PROCESSING. */
    return &pool->buffers[buffer_id].metadata;
}

component_window_state_t component_window_state(
    const component_window_pool_t *pool,
    component_window_id_t buffer_id)
{
    if ((pool == (const component_window_pool_t *)0) ||
        !component_window_valid_id(buffer_id))
    {
        return COMPONENT_WINDOW_STATE_INVALID;
    }
    return pool->buffers[buffer_id].state;
}
