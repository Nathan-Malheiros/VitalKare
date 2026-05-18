/**
 * @file vitalkare_http.h
 * @brief Cliente HTTP REST do VitalKare — substitui MQTT
 *
 * Usa exclusivamente esp_http_client.h (ESP-IDF nativo).
 * Thread-safe: pode ser chamado de múltiplas tasks.
 */

#ifndef VITALKARE_HTTP_H
#define VITALKARE_HTTP_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Inicializa o módulo HTTP (deve ser chamado após Wi-Fi conectado).
 * @return ESP_OK em sucesso.
 */
esp_err_t vitalkare_http_init(void);

/**
 * @brief Envia um payload JSON via HTTP POST.
 *
 * @param endpoint     Caminho do recurso, ex.: "/api/v1/devices/leito01/environment"
 * @param json_payload String JSON terminada em '\0'.
 * @return ESP_OK se o servidor respondeu 2xx.
 */
esp_err_t vitalkare_http_post(const char *endpoint, const char *json_payload);

/**
 * @brief Envia dados binários via HTTP POST.
 *
 * @param endpoint     Caminho do recurso.
 * @param data         Buffer binário.
 * @param len          Tamanho do buffer em bytes.
 * @param content_type MIME type, ex.: "image/jpeg" ou "audio/pcm".
 * @return ESP_OK se o servidor respondeu 2xx.
 */
esp_err_t vitalkare_http_post_binary(const char *endpoint,
                                      const uint8_t *data,
                                      size_t len,
                                      const char *content_type);

#endif /* VITALKARE_HTTP_H */
