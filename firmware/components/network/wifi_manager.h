/**
 * @file wifi_manager.h
 * @brief Gerenciamento de conexão Wi-Fi com reconexão automática e backoff exponencial
 */

#ifndef VITALKARE_WIFI_MANAGER_H
#define VITALKARE_WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

/** @brief Callback chamado quando estado Wi-Fi muda. */
typedef void (*wifi_state_cb_t)(bool connected, int8_t rssi);

/** @brief Inicializa o Wi-Fi em modo STA e começa conexão. */
esp_err_t wifi_manager_init(void); // Agora suporta configuração serial se NVS/device_config.h estiverem vazios

/** @brief Registra callback de mudança de estado. */
void wifi_manager_set_callback(wifi_state_cb_t cb);

/** @brief Retorna true se Wi-Fi está conectado e IP obtido. */
bool wifi_manager_is_connected(void);

/** @brief Retorna RSSI atual (0 se desconectado). */
int8_t wifi_manager_get_rssi(void);

/** @brief Força tentativa de reconexão imediata. */
void wifi_manager_reconnect(void);

/** @brief Task FreeRTOS do gerenciador Wi-Fi — não chamar diretamente. */
void task_wifi_manager(void *arg);

#endif /* VITALKARE_WIFI_MANAGER_H */
