/**
 * @file system_manager.h
 * @brief Orquestrador do sistema VitalKare — boot sequencing, health model,
 *        monitor periódico, shutdown controlado
 *
 * ARQUITETURA:
 *   system_manager é a única peça do firmware que conhece a topologia
 *   completa de subsistemas. Todos os outros módulos são independentes.
 *   Ele observa eventos globais (EVT_WIFI_*, EVT_TCP_*, EVT_AUDIO_*) e
 *   mantém o `system_health_t` como fonte canônica de verdade sobre o
 *   estado da plataforma.
 *
 *   Boot dependency graph (sequencial, falha em qualquer passo → modo degradado):
 *     nvs_flash → sd_manager → i2c_bus → audio_pool → display
 *     → wifi_manager (inicia wifi) → tcp_client_init
 *     → sensors_init → oximeter_init
 *
 *   A task interna (task_sys_manager) roda em baixa prioridade e executa:
 *     1. task_monitor_*()  — detectar stalls
 *     2. memory_monitor_refresh() — snapshot de memória
 *     3. Escalada de falhas → core_set_state() → log estruturado
 *     4. Reboot watchdog se stall >= TASK_MONITOR_REBOOT_THRESHOLD_MS
 *
 * CONCORRÊNCIA:
 *   - s_health protegido por mutex: escritas sempre sob mutex.
 *   - Flags hot-path (_Atomic bool): leituras sem mutex de tasks de áudio/TCP.
 *   - system_manager_report_fault(): callable de qualquer task (posta evento).
 *   - system_manager_get_health(): cópia por valor sob mutex (seguro de qualquer task).
 *
 * PROPRIEDADE DE MEMÓRIA:
 *   Único `s_health` estático (~128 bytes). Mutex em s_health.mutex.
 *   Nenhuma alocação dinâmica neste módulo.
 *
 * SHUTDOWN ORDENADO (system_manager_shutdown):
 *   audio_ctrl_stop() → wait IDLE (max 3s) →
 *   tcp flush (max 2s) → tcp_disconnect() →
 *   offline_journal_flush() → sd_manager_unmount() →
 *   esp_restart()
 */

#ifndef VITALKARE_SYSTEM_MANAGER_H
#define VITALKARE_SYSTEM_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* =========================================================
 * MODELO DE SAÚDE DO SISTEMA
 * ========================================================= */
typedef struct {
    /* Subsistemas */
    bool wifi_ok;
    bool tcp_ok;
    bool audio_ok;
    bool sd_ok;
    bool oximeter_ok;
    bool temp_sensor_ok;
    bool display_ok;
    bool journal_ok;

    /* Contadores de evento */
    uint32_t dropped_packets;   /**< Frames descartados por fila cheia */
    uint32_t audio_overruns;    /**< Erros de leitura DMA acumulados */
    uint32_t reconnect_count;   /**< Reconexões TCP desde boot */
    uint32_t alloc_failures;    /**< malloc() retornando NULL */

    /* Métricas instantâneas */
    int8_t   wifi_rssi;
    uint32_t heap_free_bytes;
    uint32_t psram_free_bytes;
    uint32_t uptime_sec;

    /* Resultado do último monitor pass */
    uint8_t  faulted_task_count;
    bool     any_task_faulted;
} system_health_t;

/* =========================================================
 * SUBSISTEMAS IDENTIFICADOS
 * ========================================================= */
typedef enum {
    SUBSYS_WIFI      = 0,
    SUBSYS_TCP       = 1,
    SUBSYS_AUDIO     = 2,
    SUBSYS_SD        = 3,
    SUBSYS_OXIMETER  = 4,
    SUBSYS_TEMP      = 5,
    SUBSYS_DISPLAY   = 6,
    SUBSYS_JOURNAL   = 7,
    SUBSYS_COUNT     = 8,
} subsystem_id_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Inicializa o módulo (aloca mutex, zera health).
 * Chamar ANTES de qualquer outra função deste módulo.
 */
esp_err_t system_manager_init(void);

/**
 * @brief Executa a sequência de boot com dependency graph.
 * Bloqueia até que todos os subsistemas críticos estejam prontos
 * (ou tenham falhado e entrado em modo degradado).
 * Chamar de main() após system_manager_init().
 */
esp_err_t system_manager_boot_sequence(void);

/**
 * @brief Registra que um subsistema está ok ou falhou.
 * Thread-safe. Atualiza s_health.{subsystem}_ok.
 *
 * @param id   Subsistema
 * @param ok   true=ok, false=falhou
 */
void system_manager_report_subsystem(subsystem_id_t id, bool ok);

/**
 * @brief Registra uma falha com descrição. Posta log + incrementa contador.
 * Thread-safe. Callable de qualquer task.
 *
 * @param subsys   Subsistema responsável
 * @param reason   String descritiva (estática, não precisa sobreviver à call)
 */
void system_manager_report_fault(subsystem_id_t subsys, const char *reason);

/**
 * @brief Incrementa contador de pacotes descartados (chamado por tcp_client).
 * Thread-safe (atômico).
 */
void system_manager_inc_dropped_packets(void);

/**
 * @brief Incrementa contador de overruns de áudio.
 * Thread-safe (atômico).
 */
void system_manager_inc_audio_overrun(void);

/**
 * @brief Incrementa contador de reconexões TCP.
 * Thread-safe (atômico).
 */
void system_manager_inc_reconnect(void);

/**
 * @brief Copia o health model atual. Thread-safe.
 * @param out  Buffer de saída (não NULL).
 */
void system_manager_get_health(system_health_t *out);

/**
 * @brief Retorna uptime em segundos desde boot.
 * Thread-safe (baseado em esp_timer_get_time()).
 */
uint32_t system_manager_get_uptime_sec(void);

/**
 * @brief Inicia shutdown ordenado e reinicia o dispositivo.
 * Bloqueia a task chamadora. Todos os subsistemas são parados na
 * ordem inversa ao boot. Termina com esp_restart().
 *
 * Chamar apenas de task_sys_manager ou de handlers de erro graves.
 * NÃO chamar de ISR.
 */
void system_manager_shutdown(const char *reason);

/** @brief Task FreeRTOS do system_manager (monitor pass periódico). */
void task_sys_manager(void *arg);

#endif /* VITALKARE_SYSTEM_MANAGER_H */
