#ifndef AI_ADAPTER_H
#define AI_ADAPTER_H

#include <stdint.h>

#include "app_types.h"

/*
 * The Keil production target enables the NanoEdge port explicitly with
 * AI_ADAPTER_ENABLE_NANOEDGE=1. Keeping the fallback disabled here prevents
 * an accidental partial build from fabricating model results.
 */
#ifndef AI_ADAPTER_ENABLE_NANOEDGE
#define AI_ADAPTER_ENABLE_NANOEDGE (0)
#endif

#define AI_ADAPTER_SAMPLE_COUNT (128U)
#define AI_ADAPTER_AXIS_COUNT   (3U)

typedef enum
{
    AI_ADAPTER_STATUS_UNAVAILABLE = 0,
    AI_ADAPTER_STATUS_READY,
    AI_ADAPTER_STATUS_ARGUMENT,
    AI_ADAPTER_STATUS_ERROR
} ai_adapter_status_t;

typedef struct
{
    /* Vendor initialization succeeded during this power cycle. */
    uint8_t initialized;
    /* Inference is allowed only while this explicit run gate is set. */
    uint8_t running;
    /* A failed first init is terminal until the next adapter initialization. */
    uint8_t init_attempted;
    uint8_t reserved;
} ai_adapter_t;

void ai_adapter_init(ai_adapter_t *adapter);
ai_adapter_status_t ai_adapter_start(ai_adapter_t *adapter);
void ai_adapter_stop(ai_adapter_t *adapter);
uint8_t ai_adapter_is_available(const ai_adapter_t *adapter);
const char *ai_adapter_status_name(const ai_adapter_t *adapter);

/*
 * Input contract is exactly 128 x 3 float samples in g, in MPU6050 XYZ order.
 * The vendor port restores raw-count floats by multiplying each value by
 * 8192.0f and performs the generated-library calls. It never casts, filters,
 * normalizes, or fabricates a result.
 */
ai_adapter_status_t ai_adapter_infer(
    ai_adapter_t *adapter,
    const float *samples,
    uint16_t sample_count,
    uint8_t axis_count,
    app_class_t *class_id,
    uint16_t *confidence_milli);

/*
 * Explicit integration boundary for the generated library. Only the vendor
 * port includes NanoEdgeAI.h/knowledge.h; the application never guesses
 * generated symbols. knowledge.h is included by exactly one .c file.
 */
#if (AI_ADAPTER_ENABLE_NANOEDGE == 1)
ai_adapter_status_t ai_adapter_vendor_init(void);
ai_adapter_status_t ai_adapter_vendor_infer(
    const float *samples,
    uint16_t sample_count,
    uint8_t axis_count,
    app_class_t *class_id,
    uint16_t *confidence_milli);
#endif

#endif /* AI_ADAPTER_H */
