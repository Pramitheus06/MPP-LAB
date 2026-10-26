#include "board_config.h"
#if ENABLE_KEYPAD
#include "keypad_filter.h"

void keypad_filter_init(keypad_filter_t *filter)
{
    filter->candidate = 0U;
    filter->frames = 0U;
    filter->locked = 0U;
}

char keypad_filter_update(keypad_filter_t *filter, uint16_t bitmap)
{
    static const char keys[16] = {
        '1', '2', '3', 'A', '4', '5', '6', 'B',
        '7', '8', '9', 'C', '*', '0', '#', 'D'
    };
    if (bitmap != filter->candidate) {
        filter->candidate = bitmap;
        filter->frames = 0U;
    }
    if (filter->frames < KEYPAD_STABLE_FRAMES) { ++filter->frames; }
    if (bitmap == 0U) {
        if (filter->frames == KEYPAD_STABLE_FRAMES) { filter->locked = 0U; }
        return '\0';
    }
    if ((bitmap & (uint16_t)(bitmap - 1U)) != 0U) {
        /* A diode-less matrix cannot reliably distinguish ghost combinations. */
        filter->locked = 1U;
        return '\0';
    }
    if ((filter->locked != 0U) || (filter->frames < KEYPAD_STABLE_FRAMES)) {
        return '\0';
    }
    filter->locked = 1U; /* One event per press, then require stable release. */
    for (uint32_t index = 0U; index < 16U; ++index) {
        if (bitmap == (uint16_t)(1UL << index)) { return keys[index]; }
    }
    return '\0';
}

uint32_t keypad_filter_released(const keypad_filter_t *filter)
{
    return (filter->candidate == 0U) &&
           (filter->frames == KEYPAD_STABLE_FRAMES) && (filter->locked == 0U);
}

#endif
