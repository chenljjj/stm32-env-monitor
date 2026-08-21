#include "ssd1306.h"

#include <string.h>

#include "app_i2c.h"

#define SSD1306_CONTROL_COMMAND         0x00U
#define SSD1306_CONTROL_DATA            0x40U
#define SSD1306_PAGE_COUNT              (SSD1306_HEIGHT / 8U)
#define SSD1306_GLYPH_WIDTH             3U
#define SSD1306_GLYPH_HEIGHT            5U

static HAL_StatusTypeDef ssd1306_send_commands(const ssd1306_t *display,
                                                I2C_HandleTypeDef *hi2c,
                                                const uint8_t *commands,
                                                uint16_t length)
{
    uint8_t buffer[29];

    if ((display == NULL) || (hi2c == NULL) || (commands == NULL) ||
        (length == 0U) || (length >= sizeof(buffer)))
    {
        return HAL_ERROR;
    }

    buffer[0] = SSD1306_CONTROL_COMMAND;
    memcpy(&buffer[1], commands, length);
    return app_i2c_transmit(hi2c, display->address_7bit, buffer, length + 1U);
}

static void ssd1306_get_glyph(char character, uint8_t rows[SSD1306_GLYPH_HEIGHT])
{
    static const uint8_t blank[SSD1306_GLYPH_HEIGHT] = { 0U, 0U, 0U, 0U, 0U };
    const uint8_t *glyph = blank;

    /* 3x5 点阵仅覆盖本项目页面所需的数字、字母和标点。 */
    switch (character)
    {
        case '0': { static const uint8_t value[] = { 7U, 5U, 5U, 5U, 7U }; glyph = value; break; }
        case '1': { static const uint8_t value[] = { 2U, 6U, 2U, 2U, 7U }; glyph = value; break; }
        case '2': { static const uint8_t value[] = { 7U, 1U, 7U, 4U, 7U }; glyph = value; break; }
        case '3': { static const uint8_t value[] = { 7U, 1U, 7U, 1U, 7U }; glyph = value; break; }
        case '4': { static const uint8_t value[] = { 5U, 5U, 7U, 1U, 1U }; glyph = value; break; }
        case '5': { static const uint8_t value[] = { 7U, 4U, 7U, 1U, 7U }; glyph = value; break; }
        case '6': { static const uint8_t value[] = { 7U, 4U, 7U, 5U, 7U }; glyph = value; break; }
        case '7': { static const uint8_t value[] = { 7U, 1U, 2U, 2U, 2U }; glyph = value; break; }
        case '8': { static const uint8_t value[] = { 7U, 5U, 7U, 5U, 7U }; glyph = value; break; }
        case '9': { static const uint8_t value[] = { 7U, 5U, 7U, 1U, 7U }; glyph = value; break; }
        case 'A': { static const uint8_t value[] = { 2U, 5U, 7U, 5U, 5U }; glyph = value; break; }
        case 'B': { static const uint8_t value[] = { 6U, 5U, 6U, 5U, 6U }; glyph = value; break; }
        case 'C': { static const uint8_t value[] = { 3U, 4U, 4U, 4U, 3U }; glyph = value; break; }
        case 'D': { static const uint8_t value[] = { 6U, 5U, 5U, 5U, 6U }; glyph = value; break; }
        case 'E': { static const uint8_t value[] = { 7U, 4U, 6U, 4U, 7U }; glyph = value; break; }
        case 'F': { static const uint8_t value[] = { 7U, 4U, 6U, 4U, 4U }; glyph = value; break; }
        case 'G': { static const uint8_t value[] = { 3U, 4U, 5U, 5U, 3U }; glyph = value; break; }
        case 'H': { static const uint8_t value[] = { 5U, 5U, 7U, 5U, 5U }; glyph = value; break; }
        case 'I': { static const uint8_t value[] = { 7U, 2U, 2U, 2U, 7U }; glyph = value; break; }
        case 'J': { static const uint8_t value[] = { 1U, 1U, 1U, 5U, 2U }; glyph = value; break; }
        case 'K': { static const uint8_t value[] = { 5U, 5U, 6U, 5U, 5U }; glyph = value; break; }
        case 'L': { static const uint8_t value[] = { 4U, 4U, 4U, 4U, 7U }; glyph = value; break; }
        case 'M': { static const uint8_t value[] = { 5U, 7U, 7U, 5U, 5U }; glyph = value; break; }
        case 'N': { static const uint8_t value[] = { 5U, 7U, 7U, 7U, 5U }; glyph = value; break; }
        case 'O': { static const uint8_t value[] = { 2U, 5U, 5U, 5U, 2U }; glyph = value; break; }
        case 'P': { static const uint8_t value[] = { 6U, 5U, 6U, 4U, 4U }; glyph = value; break; }
        case 'Q': { static const uint8_t value[] = { 2U, 5U, 5U, 7U, 3U }; glyph = value; break; }
        case 'R': { static const uint8_t value[] = { 6U, 5U, 6U, 5U, 5U }; glyph = value; break; }
        case 'S': { static const uint8_t value[] = { 3U, 4U, 2U, 1U, 6U }; glyph = value; break; }
        case 'T': { static const uint8_t value[] = { 7U, 2U, 2U, 2U, 2U }; glyph = value; break; }
        case 'U': { static const uint8_t value[] = { 5U, 5U, 5U, 5U, 7U }; glyph = value; break; }
        case 'V': { static const uint8_t value[] = { 5U, 5U, 5U, 5U, 2U }; glyph = value; break; }
        case 'W': { static const uint8_t value[] = { 5U, 5U, 7U, 7U, 5U }; glyph = value; break; }
        case 'X': { static const uint8_t value[] = { 5U, 5U, 2U, 5U, 5U }; glyph = value; break; }
        case 'Y': { static const uint8_t value[] = { 5U, 5U, 2U, 2U, 2U }; glyph = value; break; }
        case 'Z': { static const uint8_t value[] = { 7U, 1U, 2U, 4U, 7U }; glyph = value; break; }
        case ':': { static const uint8_t value[] = { 0U, 2U, 0U, 2U, 0U }; glyph = value; break; }
        case '.': { static const uint8_t value[] = { 0U, 0U, 0U, 0U, 2U }; glyph = value; break; }
        case '-': { static const uint8_t value[] = { 0U, 0U, 7U, 0U, 0U }; glyph = value; break; }
        case '%': { static const uint8_t value[] = { 5U, 1U, 2U, 4U, 5U }; glyph = value; break; }
        default: break;
    }

    memcpy(rows, glyph, SSD1306_GLYPH_HEIGHT);
}

static void ssd1306_draw_pixel(ssd1306_t *display, uint8_t x, uint8_t y)
{
    uint16_t index;

    if ((display == NULL) || (x >= SSD1306_WIDTH) || (y >= SSD1306_HEIGHT))
    {
        return;
    }

    index = (uint16_t)x + ((uint16_t)(y / 8U) * SSD1306_WIDTH);
    display->framebuffer[index] |= (uint8_t)(1U << (y % 8U));
}

static void ssd1306_draw_char(ssd1306_t *display,
                              uint8_t x,
                              uint8_t y,
                              char character,
                              uint8_t scale)
{
    uint8_t rows[SSD1306_GLYPH_HEIGHT];
    uint8_t row;
    uint8_t column;
    uint8_t scale_x;
    uint8_t scale_y;

    if (scale == 0U)
    {
        return;
    }

    ssd1306_get_glyph(character, rows);
    for (row = 0U; row < SSD1306_GLYPH_HEIGHT; ++row)
    {
        for (column = 0U; column < SSD1306_GLYPH_WIDTH; ++column)
        {
            if ((rows[row] & (uint8_t)(1U << (SSD1306_GLYPH_WIDTH - 1U - column))) != 0U)
            {
                for (scale_y = 0U; scale_y < scale; ++scale_y)
                {
                    for (scale_x = 0U; scale_x < scale; ++scale_x)
                    {
                        ssd1306_draw_pixel(display,
                                           (uint8_t)(x + (column * scale) + scale_x),
                                           (uint8_t)(y + (row * scale) + scale_y));
                    }
                }
            }
        }
    }
}

void ssd1306_init_buffer(ssd1306_t *display, uint8_t address_7bit)
{
    if (display != NULL)
    {
        display->address_7bit = address_7bit;
        ssd1306_clear(display);
    }
}

HAL_StatusTypeDef ssd1306_init(ssd1306_t *display, I2C_HandleTypeDef *hi2c)
{
    static const uint8_t init_commands[] = {
        0xAEU, 0x20U, 0x00U, 0x40U, 0xA1U, 0xC8U, 0x81U, 0x7FU,
        0xA6U, 0xA8U, 0x3FU, 0xA4U, 0xD3U, 0x00U, 0xD5U, 0x80U,
        0xD9U, 0xF1U, 0xDAU, 0x12U, 0xDBU, 0x40U, 0x8DU, 0x14U,
        0xAFU
    };
    HAL_StatusTypeDef result;

    if ((display == NULL) || (hi2c == NULL))
    {
        return HAL_ERROR;
    }

    result = app_i2c_is_device_ready(hi2c, display->address_7bit);
    if (result != HAL_OK)
    {
        return result;
    }

    result = ssd1306_send_commands(display, hi2c, init_commands, sizeof(init_commands));
    if (result == HAL_OK)
    {
        result = ssd1306_update(display, hi2c);
    }
    return result;
}

void ssd1306_clear(ssd1306_t *display)
{
    if (display != NULL)
    {
        memset(display->framebuffer, 0, sizeof(display->framebuffer));
    }
}

void ssd1306_draw_string(ssd1306_t *display,
                         uint8_t x,
                         uint8_t y,
                         const char *text,
                         uint8_t scale)
{
    uint8_t character_step;

    if ((display == NULL) || (text == NULL) || (scale == 0U))
    {
        return;
    }

    character_step = (SSD1306_GLYPH_WIDTH + 1U) * scale;
    while (*text != '\0')
    {
        ssd1306_draw_char(display, x, y, *text, scale);
        x = (uint8_t)(x + character_step);
        if (x >= SSD1306_WIDTH)
        {
            break;
        }
        ++text;
    }
}

HAL_StatusTypeDef ssd1306_update(const ssd1306_t *display,
                                 I2C_HandleTypeDef *hi2c)
{
    static const uint8_t position_commands[] = { 0x21U, 0x00U, 0x7FU, 0x22U, 0x00U, 0x07U };
    uint8_t page;
    uint8_t buffer[SSD1306_WIDTH + 1U];
    HAL_StatusTypeDef result;

    if ((display == NULL) || (hi2c == NULL))
    {
        return HAL_ERROR;
    }

    result = ssd1306_send_commands(display, hi2c, position_commands, sizeof(position_commands));
    if (result != HAL_OK)
    {
        return result;
    }

    buffer[0] = SSD1306_CONTROL_DATA;
    for (page = 0U; page < SSD1306_PAGE_COUNT; ++page)
    {
        memcpy(&buffer[1], &display->framebuffer[(uint16_t)page * SSD1306_WIDTH], SSD1306_WIDTH);
        result = app_i2c_transmit(hi2c, display->address_7bit, buffer, sizeof(buffer));
        if (result != HAL_OK)
        {
            return result;
        }
    }

    return HAL_OK;
}
