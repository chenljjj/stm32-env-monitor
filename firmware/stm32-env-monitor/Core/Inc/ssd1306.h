#ifndef SSD1306_H
#define SSD1306_H

#include "stm32f1xx_hal.h"

/* 常见 0.96 英寸 I2C OLED 的默认规格；模块不同可在此处调整。 */
#define SSD1306_WIDTH                  128U
#define SSD1306_HEIGHT                 64U
#define SSD1306_DEFAULT_ADDRESS_7BIT   0x3CU
#define SSD1306_FRAMEBUFFER_SIZE       ((SSD1306_WIDTH * SSD1306_HEIGHT) / 8U)

typedef struct
{
    uint8_t address_7bit;
    uint8_t framebuffer[SSD1306_FRAMEBUFFER_SIZE];
} ssd1306_t;

void ssd1306_init_buffer(ssd1306_t *display, uint8_t address_7bit);
HAL_StatusTypeDef ssd1306_init(ssd1306_t *display, I2C_HandleTypeDef *hi2c);
void ssd1306_clear(ssd1306_t *display);
void ssd1306_draw_string(ssd1306_t *display,
                         uint8_t x,
                         uint8_t y,
                         const char *text,
                         uint8_t scale);
HAL_StatusTypeDef ssd1306_update(const ssd1306_t *display,
                                 I2C_HandleTypeDef *hi2c);

#endif
