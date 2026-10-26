#include "button_filter.h"
uint32_t button_filter_update(button_filter_t *b,uint32_t now,uint32_t pressed)
{
    if (pressed!=b->raw) { b->raw=pressed;b->changed_at=now; }
    if (pressed!=b->stable && (uint32_t)(now-b->changed_at)>=20U) {
        b->stable=pressed;
        if (pressed) { b->pressed_at=now; }
        else { return (uint32_t)(now-b->pressed_at)>=700U ? 4U : 1U; }
    }
    return 0U;
}
