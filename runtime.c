#include <stddef.h>
#include <stdint.h>

/* Tiny freestanding C runtime helpers that GCC may synthesize at -Os. */
void *memset(void *destination, int value, size_t count)
{
    uint8_t *output = (uint8_t *)destination;
    while (count-- != 0U) {
        *output++ = (uint8_t)value;
    }
    return destination;
}

void *memcpy(void *destination, const void *source, size_t count)
{
    uint8_t *output = (uint8_t *)destination;
    const uint8_t *input = (const uint8_t *)source;
    while (count-- != 0U) {
        *output++ = *input++;
    }
    return destination;
}
