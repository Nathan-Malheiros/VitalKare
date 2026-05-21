/**
 * @file board_profile.h
 * @brief Seleção do perfil de placa do firmware VitalKare
 *
 * Troque apenas VK_BOARD_PROFILE para alternar entre a KIIRA V2 completa
 * e um ESP32-S3 DevKit usado como bancada de validação de rede/comunicação.
 */

#ifndef VITALKARE_BOARD_PROFILE_H
#define VITALKARE_BOARD_PROFILE_H

/* =========================================================
 * PERFIS DISPONÍVEIS
 * ========================================================= */
#define VK_BOARD_PROFILE_KIIRA_V2          1
#define VK_BOARD_PROFILE_ESP32S3_DEVKIT    2

/**
 * Perfil ativo do build.
 *
 * KIIRA V2          -> sobe o firmware completo da placa alvo
 * ESP32-S3 DevKit   -> sobe um firmware reduzido, focado em Wi-Fi, TCP,
 *                      heartbeat e telemetria básica para o dashboard.
 */
#define VK_BOARD_PROFILE VK_BOARD_PROFILE_KIIRA_V2

/* =========================================================
 * BITMASK DE CAPACIDADES PUBLICADA NO HANDSHAKE
 * ========================================================= */
#define VK_CAP_SYSTEM_TELEMETRY    0x00000001u
#define VK_CAP_BUTTONS             0x00000002u
#define VK_CAP_ENVIRONMENT         0x00000004u
#define VK_CAP_OXIMETER            0x00000008u
#define VK_CAP_DISPLAY             0x00000010u
#define VK_CAP_STORAGE             0x00000020u
#define VK_CAP_CAMERA              0x00000040u
#define VK_CAP_AUDIO               0x00000080u
#define VK_CAP_DEVKIT_PROFILE      0x80000000u

#if VK_BOARD_PROFILE == VK_BOARD_PROFILE_KIIRA_V2

#define VK_BOARD_NAME              "KIIRA V2"
#define VK_BOARD_TAG               "kiira-v2"
#define VK_BOARD_RUNTIME_MODE      "full"

#define VK_BOARD_HAS_BUTTONS       1
#define VK_BOARD_HAS_DISPLAY       1
#define VK_BOARD_HAS_ENV_SENSORS   1
#define VK_BOARD_HAS_OXIMETER      1
#define VK_BOARD_HAS_STORAGE       1
#define VK_BOARD_HAS_CAMERA        1
#define VK_BOARD_HAS_AUDIO         0   /* fisicamente presente, mas desativado por conflito GPIO37/PSRAM */

#define VK_BOARD_CAPABILITIES      (VK_CAP_SYSTEM_TELEMETRY \
                                    | VK_CAP_BUTTONS \
                                    | VK_CAP_ENVIRONMENT \
                                    | VK_CAP_OXIMETER \
                                    | VK_CAP_DISPLAY \
                                    | VK_CAP_STORAGE \
                                    | VK_CAP_CAMERA)

#elif VK_BOARD_PROFILE == VK_BOARD_PROFILE_ESP32S3_DEVKIT

#define VK_BOARD_NAME              "ESP32-S3 DevKit"
#define VK_BOARD_TAG               "esp32s3-devkit"
#define VK_BOARD_RUNTIME_MODE      "reduced"

#define VK_BOARD_HAS_BUTTONS       0
#define VK_BOARD_HAS_DISPLAY       0
#define VK_BOARD_HAS_ENV_SENSORS   0
#define VK_BOARD_HAS_OXIMETER      0
#define VK_BOARD_HAS_STORAGE       0
#define VK_BOARD_HAS_CAMERA        0
#define VK_BOARD_HAS_AUDIO         0

#define VK_BOARD_CAPABILITIES      (VK_CAP_SYSTEM_TELEMETRY | VK_CAP_DEVKIT_PROFILE)

#else
#error "VK_BOARD_PROFILE invalido. Use VK_BOARD_PROFILE_KIIRA_V2 ou VK_BOARD_PROFILE_ESP32S3_DEVKIT."
#endif

#endif /* VITALKARE_BOARD_PROFILE_H */
