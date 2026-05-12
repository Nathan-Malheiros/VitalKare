/**
 * @file oled_driver.h
 * @brief Driver SSD1306 via I2C para o display OLED da KIIRA V2
 */

#ifndef VITALKARE_OLED_DRIVER_H
#define VITALKARE_OLED_DRIVER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define OLED_COLS   128
#define OLED_ROWS   64
#define OLED_PAGES  (OLED_ROWS / 8)

typedef struct {
    uint8_t buffer[OLED_COLS * OLED_PAGES];
    bool    needs_flush;
} oled_ctx_t;

esp_err_t oled_init(oled_ctx_t *ctx);
void      oled_clear(oled_ctx_t *ctx);
void      oled_flush(oled_ctx_t *ctx);
void      oled_draw_string(oled_ctx_t *ctx, uint8_t x, uint8_t y, const char *str);
void      oled_draw_pixel(oled_ctx_t *ctx, uint8_t x, uint8_t y, bool on);
void      oled_draw_hline(oled_ctx_t *ctx, uint8_t x, uint8_t y, uint8_t w);
void      oled_draw_rect(oled_ctx_t *ctx, uint8_t x, uint8_t y, uint8_t w, uint8_t h);
void      oled_invert(oled_ctx_t *ctx, bool invert);

#endif /* VITALKARE_OLED_DRIVER_H */
