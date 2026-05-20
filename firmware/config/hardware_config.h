/**
 * @file hardware_config.h
 * @brief Mapeamento de GPIOs e capacidades por perfil de placa do VitalKare
 *
 * Todos os pinos e parâmetros de hardware devem ser referenciados
 * por meio deste arquivo. Nunca use números literais de GPIO diretamente
 * no código da aplicação.
 *
 * Plataforma: KIIRA V2 — ESP32-S3 Dual-core Xtensa LX7
 */

#ifndef VITALKARE_HARDWARE_CONFIG_H
#define VITALKARE_HARDWARE_CONFIG_H

#include "board_profile.h"

#if VK_BOARD_PROFILE == VK_BOARD_PROFILE_KIIRA_V2

/* =========================================================
 * BOTÕES
 * ========================================================= */
#define HW_BTN_A_GPIO           45      /**< Botão A — Solicitação do paciente */
#define HW_BTN_B_GPIO           46      /**< Botão B — Emergência */
#define HW_BTN_ACTIVE_LEVEL     0       /**< Pull-up interno: nível ativo = LOW */
#define HW_BTN_DEBOUNCE_MS      50      /**< Debounce por software em ms */

/* =========================================================
 * DISPLAY OLED — SSD1306 (I2C)
 * ========================================================= */
#define HW_OLED_I2C_PORT        I2C_NUM_0
#define HW_OLED_SDA_GPIO        14
#define HW_OLED_SCL_GPIO        21
#define HW_OLED_I2C_FREQ_HZ     400000  /**< 400 kHz (Fast Mode) */
#define HW_OLED_I2C_ADDR        0x3C    /**< Endereço padrão SSD1306 */
#define HW_OLED_WIDTH_PX        128
#define HW_OLED_HEIGHT_PX       64

/* =========================================================
 * SENSOR DE TEMPERATURA E UMIDADE — HDC1080 (I2C)
 * ========================================================= */
#define HW_HDC1080_I2C_PORT     I2C_NUM_0   /**< Compartilha barramento com OLED */
#define HW_HDC1080_I2C_ADDR     0x40
#define HW_HDC1080_I2C_FREQ_HZ  100000      /**< 100 kHz — limite do HDC1080 */

/* =========================================================
 * ACELERÔMETRO / GIROSCÓPIO — MPU6050 (I2C)
 * ========================================================= */
#define HW_MPU6050_I2C_PORT     I2C_NUM_0   /**< Compartilha barramento com OLED */
#define HW_MPU6050_I2C_ADDR     0x68        /**< AD0 = GND */
#define HW_MPU6050_I2C_FREQ_HZ  400000      /**< 400 kHz */

/* =========================================================
 * MICROFONE DIGITAL — PDM I2S
 * ========================================================= */
#define HW_MIC_I2S_PORT         I2S_NUM_0
#define HW_MIC_CLK_GPIO         2
#define HW_MIC_DATA_GPIO        37
#define HW_MIC_SAMPLE_RATE_HZ   16000       /**< 16 kHz — adequado para voz */
#define HW_MIC_BITS_PER_SAMPLE  16
#define HW_MIC_CHANNELS         1           /**< Mono */
#define HW_MIC_DMA_BUF_COUNT    8
#define HW_MIC_DMA_BUF_LEN     1024

/* =========================================================
 * CÂMERA — OV5640
 * ========================================================= */
#define HW_CAM_XCLK_GPIO        15
#define HW_CAM_SIOD_GPIO        4           /**< I2C SCCB SDA */
#define HW_CAM_SIOC_GPIO        5           /**< I2C SCCB SCL */
#define HW_CAM_Y9_GPIO          16
#define HW_CAM_Y8_GPIO          17
#define HW_CAM_Y7_GPIO          18
#define HW_CAM_Y6_GPIO          12
#define HW_CAM_Y5_GPIO          10
#define HW_CAM_Y4_GPIO          8
#define HW_CAM_Y3_GPIO          9
#define HW_CAM_Y2_GPIO          11
#define HW_CAM_VSYNC_GPIO       6
#define HW_CAM_HREF_GPIO        7
#define HW_CAM_PCLK_GPIO        13
#define HW_CAM_XCLK_FREQ_HZ     20000000    /**< 20 MHz */
#define HW_CAM_JPEG_QUALITY     12          /**< 0=melhor, 63=pior (JPEG) */

/* Alias de áudio para audio_capture.c */
#define HW_I2S_PORT             HW_MIC_I2S_PORT
#define HW_AUDIO_CLK_GPIO       HW_MIC_CLK_GPIO
#define HW_AUDIO_DATA_GPIO      HW_MIC_DATA_GPIO
#define HW_AUDIO_SAMPLE_RATE_HZ HW_MIC_SAMPLE_RATE_HZ

/* =========================================================
 * CARTÃO SD — SDMMC
 * ========================================================= */
#define HW_SD_CLK_GPIO          39
#define HW_SD_CMD_GPIO          38
#define HW_SD_D0_GPIO           40
#define HW_SD_FREQ_HZ           25000000    /**< 25 MHz — 1-bit mode */
#define HW_SD_MOUNT_POINT       "/sdcard"

/* =========================================================
 * LDR — SENSOR DE LUMINOSIDADE (ADC)
 * ========================================================= */
#define HW_LDR_ADC_GPIO         1           /**< ADC1_CH0 */
#define HW_LDR_ADC_CHANNEL      ADC_CHANNEL_0
#define HW_LDR_ADC_UNIT         ADC_UNIT_1
#define HW_LDR_ADC_ATTENUATION  ADC_ATTEN_DB_12
#define HW_LDR_ADC_BITS         ADC_BITWIDTH_12  /**< 0–4095 */

/* =========================================================
 * BUZZER / LED PWM
 * ========================================================= */
#define HW_BUZZER_GPIO          3
#define HW_BUZZER_PWM_CHANNEL   LEDC_CHANNEL_0
#define HW_BUZZER_PWM_TIMER     LEDC_TIMER_0
#define HW_BUZZER_PWM_FREQ_HZ   5000
#define HW_BUZZER_PWM_RESOLUTION LEDC_TIMER_10_BIT

/* =========================================================
 * RELÉ / MOSFET
 * ========================================================= */
#define HW_RELAY_1_GPIO         47
#define HW_RELAY_2_GPIO         48
#define HW_RELAY_ACTIVE_LEVEL   0   /**< Ativo em nível baixo */

/* =========================================================
 * ENTRADA FOTOACOPLADA
 * ========================================================= */
#define HW_OPTO_INPUT_GPIO      35

#elif VK_BOARD_PROFILE == VK_BOARD_PROFILE_ESP32S3_DEVKIT

/* =========================================================
 * ESP32-S3 DevKit — placeholders seguros para compilar módulos opcionais.
 *
 * IMPORTANTE:
 *   Esses mapeamentos NÃO significam que o periférico exista no DevKit.
 *   O boot reduzido evita inicializar display, sensores, SD, câmera e áudio.
 * ========================================================= */
#define HW_BTN_A_GPIO           0
#define HW_BTN_B_GPIO           0
#define HW_BTN_ACTIVE_LEVEL     0
#define HW_BTN_DEBOUNCE_MS      50

#define HW_OLED_I2C_PORT        I2C_NUM_0
#define HW_OLED_SDA_GPIO        8
#define HW_OLED_SCL_GPIO        9
#define HW_OLED_I2C_FREQ_HZ     400000
#define HW_OLED_I2C_ADDR        0x3C
#define HW_OLED_WIDTH_PX        128
#define HW_OLED_HEIGHT_PX       64

#define HW_HDC1080_I2C_PORT     I2C_NUM_0
#define HW_HDC1080_I2C_ADDR     0x40
#define HW_HDC1080_I2C_FREQ_HZ  100000

#define HW_MPU6050_I2C_PORT     I2C_NUM_0
#define HW_MPU6050_I2C_ADDR     0x68
#define HW_MPU6050_I2C_FREQ_HZ  400000

#define HW_MIC_I2S_PORT         I2S_NUM_0
#define HW_MIC_CLK_GPIO         4
#define HW_MIC_DATA_GPIO        5
#define HW_MIC_SAMPLE_RATE_HZ   16000
#define HW_MIC_BITS_PER_SAMPLE  16
#define HW_MIC_CHANNELS         1
#define HW_MIC_DMA_BUF_COUNT    8
#define HW_MIC_DMA_BUF_LEN      1024

#define HW_CAM_XCLK_GPIO        10
#define HW_CAM_SIOD_GPIO        11
#define HW_CAM_SIOC_GPIO        12
#define HW_CAM_Y9_GPIO          13
#define HW_CAM_Y8_GPIO          14
#define HW_CAM_Y7_GPIO          15
#define HW_CAM_Y6_GPIO          16
#define HW_CAM_Y5_GPIO          17
#define HW_CAM_Y4_GPIO          18
#define HW_CAM_Y3_GPIO          19
#define HW_CAM_Y2_GPIO          20
#define HW_CAM_VSYNC_GPIO       21
#define HW_CAM_HREF_GPIO        35
#define HW_CAM_PCLK_GPIO        36
#define HW_CAM_XCLK_FREQ_HZ     20000000
#define HW_CAM_JPEG_QUALITY     12

#define HW_I2S_PORT             HW_MIC_I2S_PORT
#define HW_AUDIO_CLK_GPIO       HW_MIC_CLK_GPIO
#define HW_AUDIO_DATA_GPIO      HW_MIC_DATA_GPIO
#define HW_AUDIO_SAMPLE_RATE_HZ HW_MIC_SAMPLE_RATE_HZ

#define HW_SD_CLK_GPIO          39
#define HW_SD_CMD_GPIO          38
#define HW_SD_D0_GPIO           40
#define HW_SD_FREQ_HZ           25000000
#define HW_SD_MOUNT_POINT       "/sdcard"

#define HW_LDR_ADC_GPIO         1
#define HW_LDR_ADC_CHANNEL      ADC_CHANNEL_0
#define HW_LDR_ADC_UNIT         ADC_UNIT_1
#define HW_LDR_ADC_ATTENUATION  ADC_ATTEN_DB_12
#define HW_LDR_ADC_BITS         ADC_BITWIDTH_12

#define HW_BUZZER_GPIO          3
#define HW_BUZZER_PWM_CHANNEL   LEDC_CHANNEL_0
#define HW_BUZZER_PWM_TIMER     LEDC_TIMER_0
#define HW_BUZZER_PWM_FREQ_HZ   5000
#define HW_BUZZER_PWM_RESOLUTION LEDC_TIMER_10_BIT

#define HW_RELAY_1_GPIO         47
#define HW_RELAY_2_GPIO         48
#define HW_RELAY_ACTIVE_LEVEL   0

#define HW_OPTO_INPUT_GPIO      35

#endif

#endif /* VITALKARE_HARDWARE_CONFIG_H */
