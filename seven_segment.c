#include "board_config.h"
#if ENABLE_TM1637
#include "seven_segment.h"
#include "config.h"
#include "platform.h"

static uint8_t display_control = 0x8FU;
static uint32_t ack_errors;

_Static_assert(SEG_CLK_PIN == 8U && SEG_DIO_PIN == 9U,
               "No-shifter TM1637 wiring requires PC8/PC9 FT pins");

static const uint8_t digit_segments[10] = {
    0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U,
    0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU
};

static void bus_delay(void)
{
    delay_us(50U); /* Conservative ~10 kHz clock for available 13k/15k pull-ups. */
}

static void bus_start(void)
{
    gpio_write(SEG_DIO_PORT, SEG_DIO_PIN, 1U);
    gpio_write(SEG_CLK_PORT, SEG_CLK_PIN, 1U);
    bus_delay();
    gpio_write(SEG_DIO_PORT, SEG_DIO_PIN, 0U);
    bus_delay();
    gpio_write(SEG_CLK_PORT, SEG_CLK_PIN, 0U);
}

static void bus_stop(void)
{
    gpio_write(SEG_CLK_PORT, SEG_CLK_PIN, 0U);
    gpio_write(SEG_DIO_PORT, SEG_DIO_PIN, 0U);
    bus_delay();
    gpio_write(SEG_CLK_PORT, SEG_CLK_PIN, 1U);
    bus_delay();
    gpio_write(SEG_DIO_PORT, SEG_DIO_PIN, 1U);
    bus_delay();
}

static void bus_write(uint8_t value)
{
    for (uint32_t bit = 0U; bit < 8U; ++bit) {
        gpio_write(SEG_CLK_PORT, SEG_CLK_PIN, 0U);
        gpio_write(SEG_DIO_PORT, SEG_DIO_PIN, value & 0x01U);
        bus_delay();
        gpio_write(SEG_CLK_PORT, SEG_CLK_PIN, 1U);
        bus_delay();
        value >>= 1U;
    }

    /* Release DIO during the module's ACK clock. */
    gpio_write(SEG_CLK_PORT, SEG_CLK_PIN, 0U);
    gpio_write(SEG_DIO_PORT, SEG_DIO_PIN, 1U);
    bus_delay();
    gpio_write(SEG_CLK_PORT, SEG_CLK_PIN, 1U);
    bus_delay();
    if (gpio_read(SEG_DIO_PORT, SEG_DIO_PIN) != 0U) { ++ack_errors; }
    gpio_write(SEG_CLK_PORT, SEG_CLK_PIN, 0U);
}

static void send_command(uint8_t command)
{
    bus_start();
    bus_write(command);
    bus_stop();
}

void seven_segment_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    (void)RCC->AHB1ENR;
    /* ST DS10693 Table 56 note 4: above VDD+0.3 V requires NO internal pulls.
     * Set open drain before switching MODER, so there is no push-pull-high
     * interval into a line externally pulled to 5 V. Idle HIGH means release.
     * The hardware contract is GPIOC pins 8/9, not arbitrary 3.3 V GPIO. */
    const uint32_t pull_mask = (3UL << (SEG_CLK_PIN * 2U)) |
                               (3UL << (SEG_DIO_PIN * 2U));
    const uint32_t pin_mask = (1UL << SEG_CLK_PIN) | (1UL << SEG_DIO_PIN);
    SEG_CLK_PORT->PUPDR &= ~pull_mask;
    SEG_CLK_PORT->OTYPER |= pin_mask;
    SEG_CLK_PORT->BSRR = pin_mask;
    gpio_mode_output(SEG_CLK_PORT, SEG_CLK_PIN, 1U);
    gpio_mode_output(SEG_DIO_PORT, SEG_DIO_PIN, 1U);
    SEG_CLK_PORT->OSPEEDR &= ~pull_mask;
    gpio_write(SEG_CLK_PORT, SEG_CLK_PIN, 1U);
    gpio_write(SEG_DIO_PORT, SEG_DIO_PIN, 1U);
    ack_errors = 0U;
    bus_delay();
    bus_stop(); /* End a transaction interrupted by an MCU reset/reconnect. */
    seven_segment_set_brightness(1U); /* Low brightness reduces supply load. */
    const uint8_t blank[4] = {0U, 0U, 0U, 0U};
    seven_segment_show_raw(blank);
}

uint32_t seven_segment_ack_errors(void) { return ack_errors; }

void seven_segment_set_brightness(uint32_t brightness)
{
    if (brightness > 7U) {
        brightness = 7U;
    }
    display_control = (uint8_t)(0x88U | brightness);
    send_command(display_control);
}

void seven_segment_show_raw(const uint8_t segments[4])
{
    send_command(0x40U); /* Automatic address increment. */
    bus_start();
    bus_write(0xC0U);
    for (uint32_t index = 0U; index < 4U; ++index) {
        bus_write(segments[index]);
    }
    bus_stop();
    send_command(display_control);
}

static void show_prefix_number(uint8_t prefix, uint32_t value)
{
    if (value > 999U) {
        value = 999U;
    }
    uint8_t segments[4];
    segments[0] = prefix;
    segments[1] = digit_segments[(value / 100U) % 10U];
    segments[2] = digit_segments[(value / 10U) % 10U];
    segments[3] = digit_segments[value % 10U];
    seven_segment_show_raw(segments);
}

void seven_segment_show_rate(uint32_t rate_hz)
{
    /* "Fxxx" where xxx is the sampling rate in kHz. */
    show_prefix_number(0x71U, rate_hz / 1000UL);
}

void seven_segment_show_timebase(uint32_t sample_rate_hz)
{
    /* "txxx" where xxx is the approximate milliseconds per division. */
    uint32_t milliseconds_per_division =
        ((CAPTURE_SAMPLE_COUNT * 1000UL) / sample_rate_hz) / 10UL;
    if (milliseconds_per_division == 0U) {
        milliseconds_per_division = 1U;
    }
    show_prefix_number(0x78U, milliseconds_per_division);
}

#endif
