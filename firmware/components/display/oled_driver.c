/**
 * @file oled_driver.c
 * @brief Driver SSD1306 128x64 via I2C — sem libs externas
 *
 * Font 5x7 ASCII embarcada (chars 32–127).
 */

#include "oled_driver.h"
#include "driver/i2c_master.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../config/hardware_config.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

static const char *TAG = "OLED";

/* =========================================================
 * Font 5x7 (apenas printable ASCII 32-127)
 * Cada char: 5 colunas de 8 bits (bit0 = linha superior)
 * ========================================================= */
static const uint8_t FONT5X7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 32 SPC */  {0x00,0x00,0x5F,0x00,0x00}, /* 33 !  */
    {0x00,0x07,0x00,0x07,0x00}, /* 34 "  */  {0x14,0x7F,0x14,0x7F,0x14}, /* 35 #  */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 36 $  */  {0x23,0x13,0x08,0x64,0x62}, /* 37 %  */
    {0x36,0x49,0x55,0x22,0x50}, /* 38 &  */  {0x00,0x05,0x03,0x00,0x00}, /* 39 '  */
    {0x00,0x1C,0x22,0x41,0x00}, /* 40 (  */  {0x00,0x41,0x22,0x1C,0x00}, /* 41 )  */
    {0x14,0x08,0x3E,0x08,0x14}, /* 42 *  */  {0x08,0x08,0x3E,0x08,0x08}, /* 43 +  */
    {0x00,0x50,0x30,0x00,0x00}, /* 44 ,  */  {0x08,0x08,0x08,0x08,0x08}, /* 45 -  */
    {0x00,0x60,0x60,0x00,0x00}, /* 46 .  */  {0x20,0x10,0x08,0x04,0x02}, /* 47 /  */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 48 0  */  {0x00,0x42,0x7F,0x40,0x00}, /* 49 1  */
    {0x42,0x61,0x51,0x49,0x46}, /* 50 2  */  {0x21,0x41,0x45,0x4B,0x31}, /* 51 3  */
    {0x18,0x14,0x12,0x7F,0x10}, /* 52 4  */  {0x27,0x45,0x45,0x45,0x39}, /* 53 5  */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 54 6  */  {0x01,0x71,0x09,0x05,0x03}, /* 55 7  */
    {0x36,0x49,0x49,0x49,0x36}, /* 56 8  */  {0x06,0x49,0x49,0x29,0x1E}, /* 57 9  */
    {0x00,0x36,0x36,0x00,0x00}, /* 58 :  */  {0x00,0x56,0x36,0x00,0x00}, /* 59 ;  */
    {0x08,0x14,0x22,0x41,0x00}, /* 60 <  */  {0x14,0x14,0x14,0x14,0x14}, /* 61 =  */
    {0x00,0x41,0x22,0x14,0x08}, /* 62 >  */  {0x02,0x01,0x51,0x09,0x06}, /* 63 ?  */
    {0x32,0x49,0x79,0x41,0x3E}, /* 64 @  */  {0x7E,0x11,0x11,0x11,0x7E}, /* 65 A  */
    {0x7F,0x49,0x49,0x49,0x36}, /* 66 B  */  {0x3E,0x41,0x41,0x41,0x22}, /* 67 C  */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 68 D  */  {0x7F,0x49,0x49,0x49,0x41}, /* 69 E  */
    {0x7F,0x09,0x09,0x09,0x01}, /* 70 F  */  {0x3E,0x41,0x49,0x49,0x7A}, /* 71 G  */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 72 H  */  {0x00,0x41,0x7F,0x41,0x00}, /* 73 I  */
    {0x20,0x40,0x41,0x3F,0x01}, /* 74 J  */  {0x7F,0x08,0x14,0x22,0x41}, /* 75 K  */
    {0x7F,0x40,0x40,0x40,0x40}, /* 76 L  */  {0x7F,0x02,0x0C,0x02,0x7F}, /* 77 M  */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 78 N  */  {0x3E,0x41,0x41,0x41,0x3E}, /* 79 O  */
    {0x7F,0x09,0x09,0x09,0x06}, /* 80 P  */  {0x3E,0x41,0x51,0x21,0x5E}, /* 81 Q  */
    {0x7F,0x09,0x19,0x29,0x46}, /* 82 R  */  {0x46,0x49,0x49,0x49,0x31}, /* 83 S  */
    {0x01,0x01,0x7F,0x01,0x01}, /* 84 T  */  {0x3F,0x40,0x40,0x40,0x3F}, /* 85 U  */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 86 V  */  {0x3F,0x40,0x38,0x40,0x3F}, /* 87 W  */
    {0x63,0x14,0x08,0x14,0x63}, /* 88 X  */  {0x07,0x08,0x70,0x08,0x07}, /* 89 Y  */
    {0x61,0x51,0x49,0x45,0x43}, /* 90 Z  */  {0x00,0x7F,0x41,0x41,0x00}, /* 91 [  */
    {0x02,0x04,0x08,0x10,0x20}, /* 92 \  */  {0x00,0x41,0x41,0x7F,0x00}, /* 93 ]  */
    {0x04,0x02,0x01,0x02,0x04}, /* 94 ^  */  {0x40,0x40,0x40,0x40,0x40}, /* 95 _  */
    {0x00,0x01,0x02,0x04,0x00}, /* 96 `  */  {0x20,0x54,0x54,0x54,0x78}, /* 97 a  */
    {0x7F,0x48,0x44,0x44,0x38}, /* 98 b  */  {0x38,0x44,0x44,0x44,0x20}, /* 99 c  */
    {0x38,0x44,0x44,0x48,0x7F}, /* 100 d */  {0x38,0x54,0x54,0x54,0x18}, /* 101 e */
    {0x08,0x7E,0x09,0x01,0x02}, /* 102 f */  {0x08,0x14,0x54,0x54,0x3C}, /* 103 g */
    {0x7F,0x08,0x04,0x04,0x78}, /* 104 h */  {0x00,0x44,0x7D,0x40,0x00}, /* 105 i */
    {0x20,0x40,0x44,0x3D,0x00}, /* 106 j */  {0x7F,0x10,0x28,0x44,0x00}, /* 107 k */
    {0x00,0x41,0x7F,0x40,0x00}, /* 108 l */  {0x7C,0x04,0x18,0x04,0x78}, /* 109 m */
    {0x7C,0x08,0x04,0x04,0x78}, /* 110 n */  {0x38,0x44,0x44,0x44,0x38}, /* 111 o */
    {0x7C,0x14,0x14,0x14,0x08}, /* 112 p */  {0x08,0x14,0x14,0x18,0x7C}, /* 113 q */
    {0x7C,0x08,0x04,0x04,0x08}, /* 114 r */  {0x48,0x54,0x54,0x54,0x20}, /* 115 s */
    {0x04,0x3F,0x44,0x40,0x20}, /* 116 t */  {0x3C,0x40,0x40,0x20,0x7C}, /* 117 u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 118 v */  {0x3C,0x40,0x30,0x40,0x3C}, /* 119 w */
    {0x44,0x28,0x10,0x28,0x44}, /* 120 x */  {0x0C,0x50,0x50,0x50,0x3C}, /* 121 y */
    {0x44,0x64,0x54,0x4C,0x44}, /* 122 z */  {0x00,0x08,0x36,0x41,0x00}, /* 123 { */
    {0x00,0x00,0x7F,0x00,0x00}, /* 124 | */  {0x00,0x41,0x36,0x08,0x00}, /* 125 } */
    {0x08,0x08,0x2A,0x1C,0x08}, /* 126 ~ */  {0xFF,0xFF,0xFF,0xFF,0xFF}, /* 127 DEL*/
};

/* =========================================================
 * CONTEXTO GLOBAL (apenas uma instância)
 * ========================================================= */
static oled_ctx_t *s_ctx = NULL;
static i2c_master_dev_handle_t s_dev_handle = NULL;

/* Endereço I2C do SSD1306 */
#define SSD1306_I2C_ADDR    0x3C

/* =========================================================
 * HELPERS I2C PARA SSD1306 (novo driver)
 * ========================================================= */
static esp_err_t ssd1306_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};  /* Co=0, D/C=0 → command */
    return i2c_master_transmit(s_dev_handle, buf, 2, pdMS_TO_TICKS(100));
}

/* Envia um bloco de dados do display GRAM */
static esp_err_t ssd1306_data(const uint8_t *data, size_t len)
{
    /* Prepend control byte 0x40 (Co=0, D/C=1 → data) */
    uint8_t buf[33];  /* max chunk = 32 bytes + 1 control */
    if (len > 32) len = 32;
    buf[0] = 0x40;
    memcpy(buf + 1, data, len);
    return i2c_master_transmit(s_dev_handle, buf, len + 1, pdMS_TO_TICKS(200));
}

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t oled_init(oled_ctx_t *ctx)
{
    if (ctx == NULL) return ESP_ERR_INVALID_ARG;

    /* Idempotente: se o handle I2C já existe, só atualiza o ponteiro de contexto */
    if (s_dev_handle != NULL) {
        s_ctx = ctx;
        ESP_LOGD(TAG, "OLED já inicializado — reutilizando handle I2C");
        return ESP_OK;
    }

    s_ctx = ctx;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
    ctx->needs_flush = false;

    /* Inicializa barramento I2C compartilhado (idempotente) */
    esp_err_t ret = i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_bus_init: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Registra SSD1306 no barramento */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = SSD1306_I2C_ADDR,
        .scl_speed_hz    = 400000,   /* 400 kHz */
    };
    ret = i2c_master_bus_add_device(i2c_bus_get_handle(), &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(10));  /* SSD1306 power-on delay */

    /* Sequência de init do SSD1306 */
    const uint8_t init_cmds[] = {
        0xAE,               /* Display OFF */
        0xD5, 0x80,         /* Clock divide ratio */
        0xA8, 0x3F,         /* Mux ratio (64 rows) */
        0xD3, 0x00,         /* Display offset */
        0x40,               /* Start line 0 */
        0x8D, 0x14,         /* Charge pump enable */
        0x20, 0x00,         /* Memory addressing: horizontal */
        0xA1,               /* Segment remap (cols 127→0) */
        0xC8,               /* COM scan direction: remapped */
        0xDA, 0x12,         /* COM pins config */
        0x81, 0xCF,         /* Contrast */
        0xD9, 0xF1,         /* Pre-charge period */
        0xDB, 0x40,         /* VCOMH deselect */
        0xA4,               /* Entire display ON from RAM */
        0xA6,               /* Normal display (not inverted) */
        0x2E,               /* Deactivate scroll */
        0xAF,               /* Display ON */
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        ret = ssd1306_cmd(init_cmds[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SSD1306 init cmd 0x%02X falhou: %s",
                     init_cmds[i], esp_err_to_name(ret));
            return ret;
        }
    }

    oled_clear(ctx);
    oled_flush(ctx);

    ESP_LOGI(TAG, "OLED SSD1306 inicializado (128x64, I2C 0x%02X)", HW_OLED_I2C_ADDR);
    return ESP_OK;
}

/* =========================================================
 * PRIMITIVAS
 * ========================================================= */
void oled_clear(oled_ctx_t *ctx)
{
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
    ctx->needs_flush = true;
}

void oled_display_on(oled_ctx_t *ctx)
{
    (void)ctx;
    if (s_dev_handle == NULL) return;
    esp_err_t ret = ssd1306_cmd(0xAF);  /* Display ON */
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "oled_display_on: I2C err %s", esp_err_to_name(ret));
    }
}

void oled_flush(oled_ctx_t *ctx)
{
    if (s_dev_handle == NULL) {
        ESP_LOGE(TAG, "oled_flush: handle NULL — init não completou");
        return;
    }

    esp_err_t ret;

    /* Define janela de exibição (toda a tela) — horizontal addressing mode */
    ret  = ssd1306_cmd(0x21);
    ret |= ssd1306_cmd(0);
    ret |= ssd1306_cmd(127);  /* col 0→127 */
    ret |= ssd1306_cmd(0x22);
    ret |= ssd1306_cmd(0);
    ret |= ssd1306_cmd(7);    /* page 0→7 */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "oled_flush: falha ao definir janela I2C (%s)",
                 esp_err_to_name(ret));
        return;
    }

    /* Envia buffer em blocos de 32 bytes */
    bool ok = true;
    for (int i = 0; i < (int)sizeof(ctx->buffer); i += 32) {
        if (ssd1306_data(&ctx->buffer[i], 32) != ESP_OK) {
            ESP_LOGE(TAG, "oled_flush: falha no bloco %d", i / 32);
            ok = false;
            break;
        }
    }

    if (ok) {
        ESP_LOGD(TAG, "OLED framebuffer flushed (%d bytes)",
                 (int)sizeof(ctx->buffer));
        ctx->needs_flush = false;
    }
}

void oled_draw_pixel(oled_ctx_t *ctx, uint8_t x, uint8_t y, bool on)
{
    if (x >= OLED_COLS || y >= OLED_ROWS) return;
    uint16_t byte_idx = (uint16_t)(y / 8) * OLED_COLS + x;
    uint8_t  bit_mask = 1u << (y % 8);
    if (on) ctx->buffer[byte_idx] |=  bit_mask;
    else    ctx->buffer[byte_idx] &= ~bit_mask;
    ctx->needs_flush = true;
}

void oled_draw_string(oled_ctx_t *ctx, uint8_t x, uint8_t y, const char *str)
{
    if (!str) return;
    uint8_t cx = x;
    uint8_t page = y / 8;
    while (*str && cx < OLED_COLS - 5) {
        uint8_t ch = (uint8_t)*str++;
        if (ch < 32 || ch > 127) ch = 32;
        const uint8_t *glyph = FONT5X7[ch - 32];
        for (int col = 0; col < 5; col++) {
            uint16_t idx = (uint16_t)page * OLED_COLS + cx + col;
            if (idx < sizeof(ctx->buffer)) {
                ctx->buffer[idx] = glyph[col];
            }
        }
        /* separator column */
        uint16_t sep_idx = (uint16_t)page * OLED_COLS + cx + 5;
        if (sep_idx < sizeof(ctx->buffer)) ctx->buffer[sep_idx] = 0;
        cx += 6;
    }
    ctx->needs_flush = true;
}

void oled_draw_hline(oled_ctx_t *ctx, uint8_t x, uint8_t y, uint8_t w)
{
    for (int i = x; i < x + w && i < OLED_COLS; i++) {
        oled_draw_pixel(ctx, (uint8_t)i, y, true);
    }
}

void oled_draw_rect(oled_ctx_t *ctx, uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    oled_draw_hline(ctx, x, y, w);
    oled_draw_hline(ctx, x, (uint8_t)(y + h - 1), w);
    for (int i = y; i < y + h; i++) {
        oled_draw_pixel(ctx, x, (uint8_t)i, true);
        oled_draw_pixel(ctx, (uint8_t)(x + w - 1), (uint8_t)i, true);
    }
}

void oled_invert(oled_ctx_t *ctx, bool invert)
{
    (void)ctx;
    ssd1306_cmd(invert ? 0xA7 : 0xA6);
}
