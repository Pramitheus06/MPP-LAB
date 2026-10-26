/* F446 adaptation of Amiruddin Siddiqui's ILI9341 drawing interface.
 * Command parameters stay in one CS transaction; pixel spans are streamed. */
#include "stm32f4xx.h"
#include "ili9341.h"
#include "spi.h"
#include "dma.h"

static uint16_t prev_y[ADC_BUFFER_SIZE];
static uint32_t previous_valid;
volatile uint32_t tft_stage;
volatile uint32_t tft_pixels_sent;

void ILI9341_CS(uint8_t state) {
    GPIOB->BSRR = 1UL << (TFT_CS_PIN + (state ? 0U : 16U));
}
void ILI9341_DC(uint8_t state) {
    GPIOB->BSRR = 1UL << (TFT_DC_PIN + (state ? 0U : 16U));
}
void ILI9341_Reset(uint8_t state) {
    GPIOB->BSRR = 1UL << (TFT_RESET_PIN + (state ? 0U : 16U));
}
void ILI9341_GPIO_Init(void) {
    const uint32_t pins[] = {TFT_CS_PIN, TFT_DC_PIN, TFT_RESET_PIN};
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;
    ILI9341_CS(HIGH); ILI9341_DC(HIGH); ILI9341_Reset(HIGH);
    for (uint32_t i = 0U; i < 3U; ++i) {
        const uint32_t pin = pins[i];
        GPIOB->MODER = (GPIOB->MODER & ~(3UL << (2U*pin))) | (1UL << (2U*pin));
        GPIOB->OTYPER &= ~(1UL << pin);
        GPIOB->PUPDR &= ~(3UL << (2U*pin));
        GPIOB->OSPEEDR |= 2UL << (2U*pin);
    }
}
static void command(uint8_t cmd, const uint8_t *data, uint32_t size) {
    ILI9341_CS(LOW); ILI9341_DC(LOW); SPI_SendByte(cmd);
    ILI9341_DC(HIGH);
    for (uint32_t i = 0U; i < size; ++i) { SPI_SendByte(data[i]); }
    ILI9341_CS(HIGH);
}
void WriteCommand(uint8_t cmd) { command(cmd, 0, 0U); }
void WriteData(uint8_t data) {
    ILI9341_CS(LOW); ILI9341_DC(HIGH); SPI_SendByte(data); ILI9341_CS(HIGH);
}
void WriteDataRaw(uint8_t data) { ILI9341_DC(HIGH); SPI_SendByte(data); }

void SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    const uint8_t columns[] = {x0 >> 8U, x0, x1 >> 8U, x1};
    const uint8_t rows[] = {y0 >> 8U, y0, y1 >> 8U, y1};
    command(ILI9341_CASET, columns, 4U);
    command(ILI9341_PASET, rows, 4U);
    ILI9341_CS(LOW); ILI9341_DC(LOW); SPI_SendByte(ILI9341_RAMWR);
    ILI9341_DC(HIGH); /* caller streams pixels, then releases CS */
}
static void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (spi_fault || x >= LCD_WIDTH || y >= LCD_HEIGHT || w == 0U || h == 0U) { return; }
    if (w > LCD_WIDTH-x) { w = LCD_WIDTH-x; }
    if (h > LCD_HEIGHT-y) { h = LCD_HEIGHT-y; }
    SetAddressWindow(x,y,x+w-1U,y+h-1U);
    for (uint32_t i=0U; i < (uint32_t)w*h; ++i) {
        SPI_SendByte(color >> 8U); SPI_SendByte(color);
        ++tft_pixels_sent;
    }
    ILI9341_CS(HIGH);
}
void DrawPixel(uint16_t x, uint16_t y, uint16_t color) { fill_rect(x,y,1U,1U,color); }
void FillScreen(uint16_t color) {
    fill_rect(0U,0U,LCD_WIDTH,LCD_HEIGHT,color);
    previous_valid = 0U;
}
void DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    if (x0 >= LCD_WIDTH || x1 >= LCD_WIDTH || y0 >= LCD_HEIGHT || y1 >= LCD_HEIGHT) { return; }
    int32_t x=x0, y=y0;
    const int32_t dx=x1>x0 ? x1-x0 : x0-x1;
    const int32_t dy=y1>y0 ? y0-y1 : y1-y0;
    const int32_t sx=x0<x1 ? 1 : -1, sy=y0<y1 ? 1 : -1;
    int32_t error=dx+dy;
    for (;;) {
        DrawPixel((uint16_t)x,(uint16_t)y,color);
        if (x==x1 && y==y1) { break; }
        const int32_t twice=2*error;
        if (twice>=dy) { error+=dy; x+=sx; }
        if (twice<=dx) { error+=dx; y+=sy; }
    }
}
static void span(uint16_t x, uint16_t y0, uint16_t y1, uint16_t color) {
    const uint16_t top = y0<y1 ? y0 : y1;
    const uint16_t bottom = y0>y1 ? y0 : y1;
    fill_rect(x,top,1U,bottom-top+1U,color);
}
static void trace(const uint16_t *points, uint16_t color) {
    for (uint16_t i=0U; i<ADC_BUFFER_SIZE-1U; ++i) {
        const uint16_t mid=(points[i]+points[i+1U])/2U;
        span(i,points[i],mid,color);
        span(i+1U,mid,points[i+1U],color);
    }
}
void TFT_DrawWaveform(volatile uint16_t *buffer) {
    if (spi_fault) { return; }
    if (previous_valid) { trace(prev_y,BACKGROUND_COLOR); }
    for (uint16_t i=0U; i<ADC_BUFFER_SIZE; ++i) {
        const uint32_t raw=buffer[i]>4095U ? 4095U : buffer[i];
        prev_y[i]=(uint16_t)(250U-raw*168U/4095U);
    }
    trace(prev_y,WAVEFORM_COLOR);
    previous_valid=1U;
}
void ILI9341_Init(void) {
    tft_stage=1U;
    ILI9341_GPIO_Init();
    ILI9341_Reset(LOW); delay_ms(20U);
    tft_stage=2U;
    ILI9341_Reset(HIGH); delay_ms(150U);
    tft_stage=3U;
    WriteCommand(ILI9341_SWRESET); delay_ms(150U);
    tft_stage=4U;
    /* Full power/timing sequence used by the existing F446 display driver. */
    static const uint8_t init[] = {
        0xCB,5,0x39,0x2C,0x00,0x34,0x02,
        0xCF,3,0x00,0xC1,0x30, 0xE8,3,0x85,0x00,0x78,
        0xEA,2,0x00,0x00, 0xED,4,0x64,0x03,0x12,0x81,
        0xF7,1,0x20, 0xC0,1,0x23, 0xC1,1,0x10,
        0xC5,2,0x3E,0x28, 0xC7,1,0x86,
        0x36,1,0x48, 0x3A,1,0x55,
        0xB1,2,0x00,0x18, 0xB6,3,0x08,0x82,0x27,
        0xF2,1,0x00, 0x26,1,0x01,
        0xE0,15,0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00,
        0xE1,15,0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F
    };
    for (uint32_t i=0U; i<sizeof(init);) {
        const uint8_t cmd=init[i++], count=init[i++];
        command(cmd,&init[i],count); i+=count;
    }
    WriteCommand(0x13U); WriteCommand(0x20U);
    tft_stage=5U;
    WriteCommand(ILI9341_SLPOUT); delay_ms(150U);
    tft_stage=6U;
    WriteCommand(ILI9341_DISPON); delay_ms(150U);
    tft_stage=7U;
    FillScreen(BACKGROUND_COLOR);
    tft_stage=8U;
}

typedef struct { char code; uint8_t rows[7]; } glyph_t;
static const glyph_t font[] = {
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}},     {'J',{7,2,2,2,18,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}},   {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}},     {'Z',{31,1,2,4,8,16,31}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}},    {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}},    {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {'.',{0,0,0,0,0,6,6}},      {':',{0,6,6,0,6,6,0}},
    {'-',{0,0,0,31,0,0,0}},     {'+',{0,4,4,31,4,4,0}},
    {'/',{1,2,2,4,8,8,16}},     {'?',{14,17,1,2,4,0,4}},
    {'=',{0,0,31,0,31,0,0}},    {' ',{0,0,0,0,0,0,0}}
};


void TFT_FillRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color) {
    fill_rect(x,y,w,h,color);
}
void TFT_Text(uint16_t x,uint16_t y,const char *text,uint16_t color) {
    while (*text && x+6U<=LCD_WIDTH && y+8U<=LCD_HEIGHT) {
        const glyph_t *glyph=&font[sizeof(font)/sizeof(font[0])-1U];
        char code=*text++;
        if (code>='a' && code<='z') { code=(char)(code-'a'+'A'); }
        for (uint32_t g=0U;g<sizeof(font)/sizeof(font[0]);++g) {
            if (font[g].code==code) { glyph=&font[g];break; }
        }
        SetAddressWindow(x,y,x+5U,y+7U);
        for (uint32_t row=0U;row<8U;++row) {
            for (uint32_t col=0U;col<6U;++col) {
                const uint16_t pixel=row<7U && col<5U &&
                    (glyph->rows[row]&(1U<<(4U-col))) ? color : BACKGROUND_COLOR;
                SPI_SendByte(pixel>>8U);SPI_SendByte(pixel);
            }
        }
        ILI9341_CS(HIGH);x+=6U;
    }
}
void TFT_ClearPlot(void) {
    fill_rect(0U,78U,LCD_WIDTH,176U,BACKGROUND_COLOR);
    previous_valid=0U;
}
