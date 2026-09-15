#include "ai_adapter.h"

void ai_adapter_init(ai_adapter_t *adapter)
{
    if (adapter == (ai_adapter_t *)0)
    {
        return;
    }
    /*
     * initialized means that the generated vendor knowledge has been loaded.
     * It is deliberately separate from running, which is the inference gate.
     */
    adapter->initialized = 0U;
    adapter->running = 0U;
    adapter->init_attempted = 0U;
    adapter->reserved = 0U;
}

ai_adapter_status_t ai_adapter_start(ai_adapter_t *adapter)
{
    if (adapter == (ai_adapter_t *)0)
    {
        return AI_ADAPTER_STATUS_ARGUMENT;
    }
    /* Starting an already-running model is idempotent. */
    if (adapter->running != 0U)
    {
        return AI_ADAPTER_STATUS_READY;
    }

#if (AI_ADAPTER_ENABLE_NANOEDGE == 1)
    /*
     * The generated library has no deinitialization API.  Keep its knowledge
     * loaded across STOP -> START and make a failed first attempt terminal
     * until the next power-cycle adapter initialization.
     */
    if (adapter->initialized != 0U)
    {
        adapter->running = 1U;
        return AI_ADAPTER_STATUS_READY;
    }
    if (adapter->init_attempted != 0U)
    {
        return AI_ADAPTER_STATUS_ERROR;
    }
    adapter->init_attempted = 1U;
    if (ai_adapter_vendor_init() != AI_ADAPTER_STATUS_READY)
    {
        adapter->running = 0U;
        return AI_ADAPTER_STATUS_ERROR;
    }
    adapter->initialized = 1U;
    adapter->running = 1U;
    return AI_ADAPTER_STATUS_READY;
#else
    adapter->running = 0U;
    return AI_ADAPTER_STATUS_UNAVAILABLE;
#endif
}

void ai_adapter_stop(ai_adapter_t *adapter)
{
    if (adapter == (ai_adapter_t *)0)
    {
        return;
    }
    /*
     * STOP only closes the application inference gate.  NanoEdge exposes no
     * vendor stop/deinit routine, so the initialized knowledge is retained.
     */
    adapter->running = 0U;
}

uint8_t ai_adapter_is_available(const ai_adapter_t *adapter)
{
    if (adapter == (const ai_adapter_t *)0)
    {
        return 0U;
    }
    return adapter->running;
}

const char *ai_adapter_status_name(const ai_adapter_t *adapter)
{
    if (adapter == (const ai_adapter_t *)0)
    {
        return "UNINITIALIZED";
    }
#if (AI_ADAPTER_ENABLE_NANOEDGE == 0)
    return "UNAVAILABLE";
#else
    if (adapter->running != 0U)
    {
        return "READY";
    }
    if (adapter->initialized != 0U)
    {
        return "STOPPED";
    }
    if (adapter->init_attempted != 0U)
    {
        return "INIT_FAILED";
    }
    return "UNINITIALIZED";
#endif
}

ai_adapter_status_t ai_adapter_infer(
    ai_adapter_t *adapter,
    const float *samples,
    uint16_t sample_count,
    uint8_t axis_count,
    app_class_t *class_id,
    uint16_t *confidence_milli)
{
    if ((adapter == (ai_adapter_t *)0) ||
        (samples == (const float *)0) ||
        (class_id == (app_class_t *)0) ||
        (confidence_milli == (uint16_t *)0) ||
        (sample_count != AI_ADAPTER_SAMPLE_COUNT) ||
        (axis_count != AI_ADAPTER_AXIS_COUNT))
    {
        return AI_ADAPTER_STATUS_ARGUMENT;
    }
    *class_id = APP_CLASS_UNKNOWN;
    *confidence_milli = 0U;
    if (adapter->running == 0U)
    {
        return AI_ADAPTER_STATUS_UNAVAILABLE;
    }

#if (AI_ADAPTER_ENABLE_NANOEDGE == 1)
    return ai_adapter_vendor_infer(samples,
                                   sample_count,
                                   axis_count,
                                   class_id,
                                   confidence_milli);
#else
    (void)samples;
    (void)sample_count;
    (void)axis_count;
    return AI_ADAPTER_STATUS_UNAVAILABLE;
#endif
}
