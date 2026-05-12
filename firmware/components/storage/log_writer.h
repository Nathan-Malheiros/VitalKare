/**
 * @file log_writer.h
 * @brief Escrita de logs estruturados no cartão SD
 *
 * Logs são escritos em formato JSON Lines (ndjson):
 * Uma entrada JSON por linha, facilitando parsing posterior.
 *
 * Arquivo rotacionado diariamente: /sdcard/logs/YYYY-MM-DD.jsonl
 */

#ifndef VITALKARE_LOG_WRITER_H
#define VITALKARE_LOG_WRITER_H

#include "esp_err.h"
#include <stdint.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_CRITICAL,
} log_level_t;

typedef enum {
    LOG_CAT_SYSTEM = 0,
    LOG_CAT_NETWORK,
    LOG_CAT_MQTT,
    LOG_CAT_SENSOR,
    LOG_CAT_AUDIO,
    LOG_CAT_CAMERA,
    LOG_CAT_BUTTON,
    LOG_CAT_STORAGE,
} log_category_t;

esp_err_t log_writer_init(void);

/**
 * @brief Grava entrada de log no SD.
 * @param level     Nível de severidade.
 * @param category  Categoria do evento.
 * @param message   Mensagem textual (máx 200 chars).
 * @param payload   JSON extra opcional (nullable).
 */
esp_err_t log_write(log_level_t level, log_category_t category,
                     const char *message, const char *payload);

/** @brief Força flush do buffer para o arquivo SD. */
esp_err_t log_flush(void);

/** @brief Task de flush periódico — não chamar diretamente. */
void task_storage_flush(void *arg);

#endif /* VITALKARE_LOG_WRITER_H */
