/**
 * @file memory_monitor.h
 * @brief Monitor de memória: heap interna, PSRAM, filas — sem alocação dinâmica
 *
 * ARQUITETURA:
 *   Snapshot periódico (a cada MEMORY_MONITOR_INTERVAL_MS) captura:
 *   - Heap interna livre (MALLOC_CAP_INTERNAL)
 *   - Heap mínima histórica (watermark baixa)
 *   - PSRAM livre (MALLOC_CAP_SPIRAM)
 *   - Ocupação de filas registradas
 *   - Contador de falhas de alocação
 *
 *   O snapshot é tomado exclusivamente na task do system_manager.
 *   Leituras de outras tasks usam a cópia estática (não triggeram scan).
 *
 * CONCORRÊNCIA:
 *   - memory_monitor_refresh(): chamado apenas por task_sys_manager.
 *   - memory_monitor_get_snapshot(): leitura sob taskENTER_CRITICAL (breve).
 *   - memory_monitor_alloc_failed(): incremento atômico; ISR-unsafe mas
 *     chamável de qualquer task.
 *   - Não há mutex — leitura é atômica por cópia de struct pequena.
 *
 * PROPRIEDADE DE MEMÓRIA:
 *   Tudo estático. Nenhuma alocação dinâmica neste módulo.
 *   Handles de fila registrados ANTES do início das tasks (setup phase).
 *
 * MODO DE FALHA:
 *   Heap livre < MEMORY_MONITOR_HEAP_LOW_KB  → system_manager entra em modo degradado
 *   PSRAM livre < MEMORY_MONITOR_PSRAM_LOW_KB → novas sessões de áudio bloqueadas
 */

#ifndef VITALKARE_MEMORY_MONITOR_H
#define VITALKARE_MEMORY_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* =========================================================
 * THRESHOLDS DE ALERTA
 * ========================================================= */
#define MEMORY_MONITOR_HEAP_LOW_KB      24U   /**< Alerta de heap baixa */
#define MEMORY_MONITOR_HEAP_CRIT_KB     12U   /**< Heap crítica — degradar */
#define MEMORY_MONITOR_PSRAM_LOW_KB    256U   /**< PSRAM baixa — bloquear áudio */
#define MEMORY_MONITOR_INTERVAL_MS    10000U  /**< Intervalo de snapshot */

/* =========================================================
 * REGISTRO DE FILAS MONITORADAS
 * ========================================================= */
#define MEMORY_MONITOR_MAX_QUEUES       8U

typedef struct {
    const char   *name;           /**< Label para diagnóstico */
    QueueHandle_t handle;         /**< Handle da fila */
    uint8_t       max_depth;      /**< Profundidade configurada da fila */
    uint8_t       current_fill;   /**< Preenchimento no último snapshot (%) */
} monitored_queue_t;

/* =========================================================
 * SNAPSHOT — estado completo de memória
 * ========================================================= */
typedef struct {
    uint32_t heap_free_bytes;        /**< Heap interna livre */
    uint32_t heap_min_ever_bytes;    /**< Watermark mínima histórica */
    uint32_t psram_free_bytes;       /**< PSRAM livre */
    uint32_t psram_total_bytes;      /**< PSRAM total */
    uint32_t alloc_failures;         /**< Falhas de malloc() acumuladas */
    uint32_t timestamp_ms;           /**< Timestamp do último snapshot */

    uint8_t  queue_count;
    monitored_queue_t queues[MEMORY_MONITOR_MAX_QUEUES];

    bool heap_low;                   /**< Abaixo de MEMORY_MONITOR_HEAP_LOW_KB */
    bool heap_critical;              /**< Abaixo de MEMORY_MONITOR_HEAP_CRIT_KB */
    bool psram_low;                  /**< Abaixo de MEMORY_MONITOR_PSRAM_LOW_KB */
} memory_snapshot_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/** @brief Inicializa monitor (chamado antes das tasks). */
void memory_monitor_init(void);

/**
 * @brief Registra uma fila para monitoramento de ocupação.
 * Chamar durante init, antes de memory_monitor_refresh().
 * @return true se registrado; false se limite atingido.
 */
bool memory_monitor_register_queue(const char *name, QueueHandle_t q,
                                   uint8_t max_depth);

/**
 * @brief Tira um snapshot atual. Chamar periodicamente no task_sys_manager.
 * NÃO chamar de ISR.
 */
void memory_monitor_refresh(void);

/**
 * @brief Retorna o último snapshot tirado.
 * Thread-safe (leitura por cópia).
 */
void memory_monitor_get_snapshot(memory_snapshot_t *out);

/**
 * @brief Incrementa contador de falhas de alocação.
 * Chamar ao detectar malloc() retornando NULL.
 * Thread-safe (operação atômica).
 */
void memory_monitor_alloc_failed(void);

/** @brief Retorna true se heap está em estado crítico (último snapshot). */
bool memory_monitor_is_heap_critical(void);

/** @brief Retorna true se PSRAM está baixa (último snapshot). */
bool memory_monitor_is_psram_low(void);

#endif /* VITALKARE_MEMORY_MONITOR_H */
