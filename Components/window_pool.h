#ifndef COMPONENT_WINDOW_POOL_H
#define COMPONENT_WINDOW_POOL_H

#include <stdint.h>

#include "app_types.h"

#define COMPONENT_WINDOW_POOL_COUNT (2U)
#define COMPONENT_WINDOW_SAMPLE_COUNT (128U)
#define COMPONENT_WINDOW_AXIS_COUNT (3U)
#define COMPONENT_WINDOW_ID_INVALID (0xFFU)

typedef uint8_t component_window_id_t;

typedef enum
{
    COMPONENT_WINDOW_STATE_FREE = 0,
    COMPONENT_WINDOW_STATE_FILLING,
    COMPONENT_WINDOW_STATE_READY,
    COMPONENT_WINDOW_STATE_PROCESSING,
    COMPONENT_WINDOW_STATE_INVALID
} component_window_state_t;

typedef enum
{
    COMPONENT_WINDOW_OK = 0,
    COMPONENT_WINDOW_INVALID_ID,
    COMPONENT_WINDOW_NOT_OWNER,
    COMPONENT_WINDOW_INVALID_ARGUMENT
} component_window_status_t;

typedef struct
{
    uint32_t sequence;
    uint32_t timestamp_tick;
    uint32_t session_id;
    uint16_t sample_count;
    app_label_t label;
    uint8_t reserved;
} component_window_metadata_t;

typedef struct
{
    int16_t samples[COMPONENT_WINDOW_SAMPLE_COUNT][COMPONENT_WINDOW_AXIS_COUNT];
    uint32_t sample_ticks[COMPONENT_WINDOW_SAMPLE_COUNT];
    component_window_metadata_t metadata;
    volatile component_window_state_t state;
} component_window_buffer_t;

/*
 * The pool owns storage and state only.  FreeRTOS queues in App/ own the IDs;
 * keeping queue state here would create a second, conflicting ownership graph.
 */
typedef struct
{
    component_window_buffer_t buffers[COMPONENT_WINDOW_POOL_COUNT];
} component_window_pool_t;

void component_window_pool_init(component_window_pool_t *pool);

/* State transitions are performed after the corresponding RTOS queue action. */
component_window_status_t component_window_begin_fill(
    component_window_pool_t *pool,
    component_window_id_t buffer_id);
component_window_status_t component_window_commit_ready(
    component_window_pool_t *pool,
    component_window_id_t buffer_id,
    const component_window_metadata_t *metadata);
component_window_status_t component_window_begin_processing(
    component_window_pool_t *pool,
    component_window_id_t buffer_id);
component_window_status_t component_window_release_free(
    component_window_pool_t *pool,
    component_window_id_t buffer_id);
component_window_status_t component_window_abort_fill(
    component_window_pool_t *pool,
    component_window_id_t buffer_id);
component_window_status_t component_window_abort_ready(
    component_window_pool_t *pool,
    component_window_id_t buffer_id);

/* Data and metadata are exposed only to the current owner state. */
int16_t *component_window_write_data(
    component_window_pool_t *pool,
    component_window_id_t buffer_id);
const int16_t *component_window_read_data(
    const component_window_pool_t *pool,
    component_window_id_t buffer_id);
uint32_t *component_window_write_ticks(
    component_window_pool_t *pool,
    component_window_id_t buffer_id);
const uint32_t *component_window_read_ticks(
    const component_window_pool_t *pool,
    component_window_id_t buffer_id);
component_window_metadata_t *component_window_write_metadata(
    component_window_pool_t *pool,
    component_window_id_t buffer_id);
const component_window_metadata_t *component_window_read_metadata(
    const component_window_pool_t *pool,
    component_window_id_t buffer_id);

component_window_state_t component_window_state(
    const component_window_pool_t *pool,
    component_window_id_t buffer_id);

#endif /* COMPONENT_WINDOW_POOL_H */
