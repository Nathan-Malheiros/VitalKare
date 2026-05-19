/**
 * @file task_monitor.h
 * @brief Monitor de liveness por task — detecção de stall em nível de aplicação
 *
 * ARQUITETURA:
 *   Cada task crítica chama task_monitor_heartbeat(id) no topo do seu loop
 *   principal. O system_manager faz varreduras periódicas chamando
 *   task_monitor_stall_ms(id) para detectar tasks que pararam de processar.
 *
 *   Distinção de hardware WDT (watchdog.h):
 *   - Hardware WDT: detecta travamento total da CPU (tarefa não libera CPU)
 *   - task_monitor: detecta stall lógico (task roda mas não processa eventos)
 *
 *   Exemplo: task_tcp_tx pode estar em xEventGroupWaitBits mas nunca enviar
 *   dados por 30s. Hardware WDT não detecta; task_monitor sim.
 *
 * CONCORRÊNCIA:
 *   - task_monitor_heartbeat(): escrita atômica, chamável de qualquer task.
 *   - task_monitor_stall_ms(): leitura atômica, thread-safe.
 *   - Nenhum mutex — `_Atomic int64_t` garante consistência por slot.
 *   - ISR-UNSAFE: NÃO chamar de ISR (esp_timer_get_time() não é ISR-safe).
 *
 * PROPRIEDADE DE MEMÓRIA:
 *   Array estático de TASK_ID_COUNT × 8 bytes em BSS. Zero alocação dinâmica.
 *
 * MODO DE FALHA:
 *   Se uma task trava, seu timestamp congela. task_monitor_stall_ms()
 *   retorna tempo crescente. system_manager escala para reboot quando
 *   stall > TASK_MONITOR_REBOOT_THRESHOLD_MS.
 */

#ifndef VITALKARE_TASK_MONITOR_H
#define VITALKARE_TASK_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================
 * IDs DE TASK — manter em sync com as tasks em main.c
 * ========================================================= */
typedef enum {
    TASK_ID_TCP_TX       = 0,
    TASK_ID_TCP_RX       = 1,
    TASK_ID_AUDIO_CTRL   = 2,
    TASK_ID_AUDIO_ENC    = 3,
    TASK_ID_DISPATCHER   = 4,
    TASK_ID_SENSOR       = 5,
    TASK_ID_OXIMETER     = 6,
    TASK_ID_DISPLAY      = 7,
    TASK_ID_SYS_MANAGER  = 8,
    TASK_ID_COUNT        = 9,
} task_id_t;

/* =========================================================
 * THRESHOLDS
 * ========================================================= */

/** Stall considerado "degradado" — task viva mas lenta */
#define TASK_MONITOR_WARN_THRESHOLD_MS    5000U

/** Stall que aciona escalada de falha ao system_manager */
#define TASK_MONITOR_FAULT_THRESHOLD_MS  15000U

/** Stall que aciona reboot via watchdog */
#define TASK_MONITOR_REBOOT_THRESHOLD_MS 30000U

/* =========================================================
 * SNAPSHOT — estado completo para diagnóstico
 * ========================================================= */
typedef struct {
    task_id_t id;
    const char *name;           /**< Nome human-readable da task */
    uint32_t   stall_ms;        /**< Ms desde último heartbeat (0 = ok) */
    bool       registered;      /**< Task registrou pelo menos 1 heartbeat */
    bool       faulted;         /**< Stall ≥ TASK_MONITOR_FAULT_THRESHOLD_MS */
} task_monitor_snapshot_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/** @brief Inicializa monitor (zera timestamps). Chamar antes das tasks. */
void task_monitor_init(void);

/**
 * @brief Registra heartbeat da task atual.
 *
 * Thread-safe. Escrita atômica no timestamp da task `id`.
 * Chamar no topo do loop principal de cada task crítica.
 *
 * @param id  Identificador da task (de task_id_t).
 */
void task_monitor_heartbeat(task_id_t id);

/**
 * @brief Retorna ms desde o último heartbeat da task.
 *
 * @param id  Identificador da task.
 * @return    Milissegundos de stall. 0 se nunca registrou heartbeat.
 *            INT32_MAX se a task nunca registrou nenhum heartbeat.
 */
uint32_t task_monitor_stall_ms(task_id_t id);

/**
 * @brief Preenche snapshot completo da task.
 * Thread-safe; leitura atômica.
 */
void task_monitor_get_snapshot(task_id_t id, task_monitor_snapshot_t *out);

/**
 * @brief Retorna true se QUALQUER task crítica está em fault.
 * Útil para verificação rápida no system_manager.
 */
bool task_monitor_any_faulted(void);

/**
 * @brief Itera todos os IDs faultados.
 * @param out_ids  Buffer de saída (tamanho ≥ TASK_ID_COUNT).
 * @param out_count Número de IDs escritos.
 */
void task_monitor_get_faulted(task_id_t *out_ids, uint8_t *out_count);

/** @brief Retorna nome string do task_id_t (para logs). */
const char *task_monitor_name(task_id_t id);

#endif /* VITALKARE_TASK_MONITOR_H */
