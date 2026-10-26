#include "board_config.h"
#if ENABLE_KEYPAD
#include "controls.h"
#include "config.h"
#include "keypad_filter.h"
#include "platform.h"

#define ROW_MASK (0xFUL << KEYPAD_ROW_FIRST_PIN)
#define COL_MASK (0xFUL << KEYPAD_COL_FIRST_PIN)
#define KEY_QUEUE_SIZE 16U
static volatile uint32_t initialized, wake_pending;
static volatile uint32_t queue_head, queue_tail, dropped_keys;
static char key_queue[KEY_QUEUE_SIZE];
/* SysTick owns scanning/filter state; EXTI only requests a scan. */
static uint32_t scanning, column;
static uint16_t matrix;
static keypad_filter_t filter;

static void select_column(uint32_t selected)
{
    KEYPAD_COL_PORT->BSRR = COL_MASK; /* Release other open-drain columns. */
    KEYPAD_COL_PORT->BSRR = 1UL << (KEYPAD_COL_FIRST_PIN + selected + 16U);
}

static void enter_idle(void)
{
    scanning = 0U;
    KEYPAD_COL_PORT->BSRR = COL_MASK << 16U; /* All low: any key pulls its row low. */
    EXTI->PR = ROW_MASK;
    wake_pending = 0U;
    EXTI->IMR |= ROW_MASK;
    /* An idle row poll catches a press coinciding with EXTI re-enabling. */
}

void controls_init(void)
{
    const uint32_t saved = irq_save();
    initialized = 0U;
    queue_head = 0U; queue_tail = 0U; dropped_keys = 0U;
    keypad_filter_init(&filter);
    RCC->AHB1ENR |= (1UL << 1U) | (1UL << 2U);
    (void)RCC->AHB1ENR;
    for (uint32_t index = 0U; index < 4U; ++index) {
        gpio_mode_input(KEYPAD_ROW_PORT, KEYPAD_ROW_FIRST_PIN + index, 1U);
        gpio_write(KEYPAD_COL_PORT, KEYPAD_COL_FIRST_PIN + index, 1U);
        gpio_mode_output(KEYPAD_COL_PORT, KEYPAD_COL_FIRST_PIN + index, 1U);
        KEYPAD_COL_PORT->OSPEEDR &= ~(3UL << ((KEYPAD_COL_FIRST_PIN + index) * 2U));
    }
    RCC->APB2ENR |= 1UL << 14U; /* SYSCFG. */
    (void)RCC->APB2ENR;
    SYSCFG->EXTICR[1] = (SYSCFG->EXTICR[1] & ~0xFFFFUL) | 0x2222UL;
    EXTI->IMR &= ~ROW_MASK;
    EXTI->RTSR &= ~ROW_MASK;
    EXTI->FTSR |= ROW_MASK;
    NVIC_SetPriority(EXTI4_IRQn, 4U);
    NVIC_SetPriority(EXTI9_5_IRQn, 4U);
    NVIC_EnableIRQ(EXTI4_IRQn);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
    enter_idle();
    initialized = 1U;
    irq_restore(saved);
}

void controls_tick_1ms(void)
{
    if (initialized == 0U) { return; }
    if (scanning == 0U) {
        if ((wake_pending == 0U) && ((KEYPAD_ROW_PORT->IDR & ROW_MASK) == ROW_MASK)) { return; }
        EXTI->IMR &= ~ROW_MASK; /* Mask scan-generated row transitions. */
        wake_pending = 0U; matrix = 0U; column = 0U; scanning = 1U;
        select_column(column);
        return; /* Settle for one tick before reading this column. */
    }
    const uint32_t rows = ((~KEYPAD_ROW_PORT->IDR) & ROW_MASK) >> KEYPAD_ROW_FIRST_PIN;
    for (uint32_t row = 0U; row < 4U; ++row) {
        if ((rows & (1UL << row)) != 0U) { matrix |= (uint16_t)(1UL << (row * 4U + column)); }
    }
    ++column;
    if (column == 4U) {
        const char key = keypad_filter_update(&filter, matrix);
        if (key != '\0') {
            const uint32_t next = (queue_head + 1U) % KEY_QUEUE_SIZE;
            if (next != queue_tail) {
                key_queue[queue_head] = key; queue_head = next;
            } else { ++dropped_keys; } /* Drop newest; retain unread keys. */
        }
        if (keypad_filter_released(&filter) != 0U) { enter_idle(); return; }
        matrix = 0U; column = 0U;
    }
    select_column(column);
}

char controls_take_key(void)
{
    const uint32_t saved = irq_save();
    char key = '\0';
    if (queue_tail != queue_head) {
        key = key_queue[queue_tail]; queue_tail = (queue_tail + 1U) % KEY_QUEUE_SIZE;
    }
    irq_restore(saved);
    return key;
}

uint32_t controls_dropped_keys(void) { return dropped_keys; }

static void request_scan(uint32_t pending)
{
    EXTI->PR = pending; EXTI->IMR &= ~ROW_MASK; wake_pending = 1U;
}

void EXTI4_IRQHandler(void)
{
    const uint32_t pending = EXTI->PR & (1UL << KEYPAD_ROW_FIRST_PIN);
    if (pending != 0U) { request_scan(pending); }
}

void EXTI9_5_IRQHandler(void)
{
    const uint32_t pending = EXTI->PR & (ROW_MASK & ~(1UL << KEYPAD_ROW_FIRST_PIN));
    if (pending != 0U) { request_scan(pending); }
}

#endif
