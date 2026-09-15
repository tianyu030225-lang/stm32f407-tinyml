#include "ai_adapter.h"

#if (AI_ADAPTER_ENABLE_NANOEDGE == 1)

#include "NanoEdgeAI.h"
#include "knowledge.h"

#if (AXIS_NUMBER != AI_ADAPTER_AXIS_COUNT)
#error "NanoEdge AXIS_NUMBER must remain 3 for the MPU6050 XYZ contract"
#endif

#if (DATA_INPUT_USER != 1)
#error "NanoEdge DATA_INPUT_USER must remain 1 for the row-by-row adapter"
#endif

#if (CLASS_NUMBER != 3)
#error "This adapter requires the three Result178 classes"
#endif

#define AI_VENDOR_RAW_COUNTS_PER_G (8192.0f)

static uint8_t ai_vendor_probability_is_valid(float probability)
{
    /*
     * NaN is the only value for which value != value.  The range checks also
     * reject both infinities without depending on a toolchain math macro.
     */
    if (probability != probability)
    {
        return 0U;
    }
    if ((probability < 0.0f) || (probability > 1.0f))
    {
        return 0U;
    }
    return 1U;
}

static app_class_t ai_vendor_map_class(uint16_t vendor_id)
{
    /*
     * Result178 vendor order/IDs are [1 VIBRATION, 2 STABLE, 3 IMPACT].
     * The application enum is [0 UNKNOWN, 1 STABLE, 2 VIBRATION, 3 IMPACT].
     */
    switch (vendor_id)
    {
        case 1U:
            return APP_CLASS_VIBRATION;
        case 2U:
            return APP_CLASS_STABLE;
        case 3U:
            return APP_CLASS_IMPACT;
        case 0U:
        default:
            return APP_CLASS_UNKNOWN;
    }
}

static uint8_t ai_vendor_confidence_from_probability(
    float probability,
    uint16_t *confidence_milli)
{
    float scaled_confidence;

    if ((confidence_milli == (uint16_t *)0) ||
        (ai_vendor_probability_is_valid(probability) == 0U))
    {
        return 0U;
    }

    scaled_confidence = probability * 1000.0f;
    if (scaled_confidence <= 0.0f)
    {
        *confidence_milli = 0U;
    }
    else if (scaled_confidence >= 1000.0f)
    {
        *confidence_milli = 1000U;
    }
    else
    {
        /*
         * Round half-up after scaling, then truncate only the final bounded
         * integer representation. This cast never touches the raw input.
         */
        scaled_confidence += 0.5f;
        *confidence_milli = (uint16_t)scaled_confidence;
        if (*confidence_milli > 1000U)
        {
            *confidence_milli = 1000U;
        }
    }
    return 1U;
}

ai_adapter_status_t ai_adapter_vendor_init(void)
{
    enum neai_state vendor_status;

    vendor_status = neai_classification_init(knowledge);
    if (vendor_status != NEAI_OK)
    {
        return AI_ADAPTER_STATUS_ERROR;
    }
    return AI_ADAPTER_STATUS_READY;
}

ai_adapter_status_t ai_adapter_vendor_infer(
    const float *samples,
    uint16_t sample_count,
    uint8_t axis_count,
    app_class_t *class_id,
    uint16_t *confidence_milli)
{
    float data_input[DATA_INPUT_USER * AXIS_NUMBER];
    float output_buffer[CLASS_NUMBER];
    float probability_sums[CLASS_NUMBER];
    float averaged_probabilities[CLASS_NUMBER];
    float winning_probability;
    enum neai_state vendor_status;
    uint16_t vendor_id;
    uint16_t vendor_index;
    uint16_t sample_index;
    uint8_t axis_index;
    uint8_t class_index;
    uint8_t winning_index;
    app_class_t mapped_class;

    if ((samples == (const float *)0) ||
        (class_id == (app_class_t *)0) ||
        (confidence_milli == (uint16_t *)0) ||
        (sample_count != AI_ADAPTER_SAMPLE_COUNT) ||
        (axis_count != AI_ADAPTER_AXIS_COUNT))
    {
        return AI_ADAPTER_STATUS_ARGUMENT;
    }

    *class_id = APP_CLASS_UNKNOWN;
    *confidence_milli = 0U;
    for (class_index = 0U;
         class_index < CLASS_NUMBER;
         class_index++)
    {
        probability_sums[class_index] = 0.0f;
    }

    for (sample_index = 0U;
         sample_index < AI_ADAPTER_SAMPLE_COUNT;
         sample_index++)
    {
        /*
         * The application boundary is float32 g in XYZ order. NanoEdge
         * Result178 was generated for raw-count floats, so restore the raw
         * representation exactly as a float: no integer cast, normalization,
         * filtering, or axis reorder.
         */
        for (axis_index = 0U;
             axis_index < AXIS_NUMBER;
             axis_index++)
        {
            data_input[axis_index] =
                samples[(sample_index * axis_count) + axis_index] *
                AI_VENDOR_RAW_COUNTS_PER_G;
        }

        vendor_id = 0U;
        vendor_status = neai_classification(data_input,
                                            output_buffer,
                                            &vendor_id);
        if (vendor_status != NEAI_OK)
        {
            return AI_ADAPTER_STATUS_ERROR;
        }

        /*
         * id_class is a per-call validity/consistency signal only. The
         * window decision below is made from averaged probabilities, never
         * by voting the 128 returned IDs.
         */
        if ((vendor_id == 0U) || (vendor_id > CLASS_NUMBER))
        {
            return AI_ADAPTER_STATUS_ERROR;
        }
        vendor_index = (uint16_t)(vendor_id - 1U);
        for (class_index = 0U;
             class_index < CLASS_NUMBER;
             class_index++)
        {
            if (ai_vendor_probability_is_valid(
                    output_buffer[class_index]) == 0U)
            {
                return AI_ADAPTER_STATUS_ERROR;
            }
            if (output_buffer[vendor_index] < output_buffer[class_index])
            {
                return AI_ADAPTER_STATUS_ERROR;
            }
            probability_sums[class_index] += output_buffer[class_index];
        }
    }

    for (class_index = 0U;
         class_index < CLASS_NUMBER;
         class_index++)
    {
        averaged_probabilities[class_index] =
            probability_sums[class_index] /
            (float)AI_ADAPTER_SAMPLE_COUNT;
        if (ai_vendor_probability_is_valid(
                averaged_probabilities[class_index]) == 0U)
        {
            return AI_ADAPTER_STATUS_ERROR;
        }
    }

    /*
     * Strictly greater keeps the lowest vendor index on a tie:
     * VIBRATION (0), then STABLE (1), then IMPACT (2).
     */
    winning_index = 0U;
    winning_probability = averaged_probabilities[0U];
    for (class_index = 1U;
         class_index < CLASS_NUMBER;
         class_index++)
    {
        if (averaged_probabilities[class_index] > winning_probability)
        {
            winning_index = class_index;
            winning_probability = averaged_probabilities[class_index];
        }
    }

    mapped_class = ai_vendor_map_class((uint16_t)winning_index + 1U);
    if (mapped_class == APP_CLASS_UNKNOWN)
    {
        return AI_ADAPTER_STATUS_ERROR;
    }
    if (ai_vendor_confidence_from_probability(
            winning_probability,
            confidence_milli) == 0U)
    {
        return AI_ADAPTER_STATUS_ERROR;
    }

    *class_id = mapped_class;
    return AI_ADAPTER_STATUS_READY;
}

#endif /* (AI_ADAPTER_ENABLE_NANOEDGE == 1) */
