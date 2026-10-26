#include "analysis.h"
#include "config.h"

static uint32_t raw_to_mv(uint32_t raw)
{
    return (raw * ADC_REFERENCE_MV + (ADC_FULL_SCALE_COUNTS / 2UL)) /
           ADC_FULL_SCALE_COUNTS;
}

#if DSO_INPUT_AFE
static int32_t divide_round_nearest(int32_t numerator, int32_t denominator)
{
    if (numerator >= 0) {
        return (numerator + (denominator / 2)) / denominator;
    }
    return (numerator - (denominator / 2)) / denominator;
}
#endif

int32_t analysis_adc_mv_to_input_mv(uint32_t adc_mv)
{
#if DSO_INPUT_AFE
    const int32_t scaled =
        (AFE_ZERO_MV - (int32_t)adc_mv) * AFE_GAIN_DENOMINATOR;
    return divide_round_nearest(scaled, AFE_GAIN_NUMERATOR);
#else
    return (int32_t)adc_mv;
#endif
}

void analysis_compute(const uint16_t *samples, uint32_t count,
                      uint32_t sample_rate_hz, uint16_t threshold,
                      signal_measurements_t *result)
{
    if ((samples == 0) || (result == 0) || (count == 0U)) {
        return;
    }

    uint16_t minimum = samples[0];
    uint16_t maximum = samples[0];
    uint64_t sum = 0U;
    uint32_t crossing_count = 0U;
    uint32_t first_crossing = 0U;
    uint32_t last_crossing = 0U;

    for (uint32_t index = 0U; index < count; ++index) {
        const uint16_t value = samples[index];
        if (value < minimum) {
            minimum = value;
        }
        if (value > maximum) {
            maximum = value;
        }
        sum += value;

        if ((index > 0U) && (samples[index - 1U] < threshold) &&
            (value >= threshold)) {
            if (crossing_count == 0U) {
                first_crossing = index;
            }
            last_crossing = index;
            ++crossing_count;
        }
    }

    result->minimum_raw = minimum;
    result->maximum_raw = maximum;
    result->average_raw = (uint16_t)(sum / count);
    result->minimum_mv = raw_to_mv(minimum);
    result->maximum_mv = raw_to_mv(maximum);
    result->average_mv = raw_to_mv(result->average_raw);
#if DSO_INPUT_AFE
    /* The AFE is inverting, so ADC maximum is connector minimum. */
    result->input_minimum_mv =
        analysis_adc_mv_to_input_mv(result->maximum_mv);
    result->input_maximum_mv =
        analysis_adc_mv_to_input_mv(result->minimum_mv);
    result->input_average_mv =
        analysis_adc_mv_to_input_mv(result->average_mv);
    result->peak_to_peak_mv =
        ((result->maximum_mv - result->minimum_mv) *
         (uint32_t)AFE_GAIN_DENOMINATOR +
         ((uint32_t)AFE_GAIN_NUMERATOR / 2UL)) /
        (uint32_t)AFE_GAIN_NUMERATOR;
#else
    result->input_minimum_mv = (int32_t)result->minimum_mv;
    result->input_maximum_mv = (int32_t)result->maximum_mv;
    result->input_average_mv = (int32_t)result->average_mv;
    result->peak_to_peak_mv = result->maximum_mv - result->minimum_mv;
#endif
    result->frequency_hz = 0U;

    if ((crossing_count >= 2U) && (last_crossing > first_crossing)) {
        const uint64_t numerator =
            (uint64_t)sample_rate_hz * (uint64_t)(crossing_count - 1U);
        result->frequency_hz =
            (uint32_t)((numerator + ((last_crossing - first_crossing) / 2U)) /
                       (last_crossing - first_crossing));
    }
}
