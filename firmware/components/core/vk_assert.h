/**
 * @file vk_assert.h
 * @brief VitalKare Assertion Framework — invariant validation, ownership checks, fault logging
 *
 * ARCHITECTURE RATIONALE:
 *   Invariant violations in embedded code silently corrupt state across tasks.
 *   A centralized assertion layer makes every violation observable and reproducible,
 *   with enough context to diagnose from a single log line.
 *
 * CATEGORIES:
 *   VK_ASSERT_FATAL   — violation is unrecoverable; log + esp_restart()
 *   VK_ASSERT_WARN    — violation is notable but survivable; log only
 *   VK_ASSERT_NOT_ISR — call-site must not be in ISR context (fatal if violated)
 *   VK_ASSERT_NOT_NULL — NULL pointer is always fatal
 *   VK_ASSERT_PSRAM   — pointer must be in external PSRAM (warn only)
 *   VK_ASSERT_QUEUE   — queue handle must not be NULL (fatal)
 *   VK_ASSERT_FSM     — FSM state transition is valid (warn + log from/to)
 *
 * COMPILE-TIME GATE:
 *   Define VK_ASSERT_DISABLE to strip all assertion macros (release build only).
 *   Default: ENABLED.
 *
 * ISR SAFETY:
 *   VK_ASSERT_WARN uses ESP_LOGW — NOT safe from ISR.
 *   VK_ASSERT_FATAL uses ESP_LOGE + esp_restart() — NOT safe from ISR.
 *   VK_ASSERT_NOT_ISR is the only macro that intentionally runs in ISR paths to
 *   detect incorrect call context.
 *
 * MEMORY:
 *   Pure header — zero heap, zero stack beyond what the logging framework uses.
 *   All macros expand inline at the call site.
 */

#pragma once

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <string.h>

#define _VK_ASSERT_TAG  "ASSERT"

/* =========================================================
 * GLOBAL ENABLE/DISABLE GATE
 * ========================================================= */
#ifndef VK_ASSERT_DISABLE

/* =========================================================
 * FATAL — log + reboot
 *
 * Usage: VK_ASSERT_FATAL(ptr != NULL, "chunk pool pointer is NULL");
 * When condition is FALSE: ESP_LOGE + esp_restart()
 * ========================================================= */
#define VK_ASSERT_FATAL(condition, ...)                                        \
    do {                                                                       \
        if (!(condition)) {                                                    \
            ESP_LOGE(_VK_ASSERT_TAG,                                           \
                     "FATAL @ %s:%d [%s]: " __VA_ARGS__,                       \
                     __FILE__, __LINE__, __func__);                             \
            esp_restart();                                                     \
        }                                                                      \
    } while (0)

/* =========================================================
 * WARN — log only, execution continues
 *
 * Usage: VK_ASSERT_WARN(seq > 0, "seq number rolled back: got %u", seq);
 * ========================================================= */
#define VK_ASSERT_WARN(condition, ...)                                         \
    do {                                                                       \
        if (!(condition)) {                                                    \
            ESP_LOGW(_VK_ASSERT_TAG,                                           \
                     "WARN @ %s:%d [%s]: " __VA_ARGS__,                        \
                     __FILE__, __LINE__, __func__);                             \
        }                                                                      \
    } while (0)

/* =========================================================
 * NOT-NULL — NULL pointer is always unrecoverable
 * ========================================================= */
#define VK_ASSERT_NOT_NULL(ptr)                                                \
    VK_ASSERT_FATAL((ptr) != NULL, #ptr " is NULL")

/* =========================================================
 * QUEUE HANDLE — NULL queue handle is always unrecoverable
 * ========================================================= */
#define VK_ASSERT_QUEUE(q)                                                     \
    VK_ASSERT_FATAL((q) != NULL, "Queue handle '" #q "' is NULL")

/* =========================================================
 * NOT-ISR — function must not be called from interrupt context
 *
 * Prevents mutex acquisition, malloc, and logging APIs from
 * being called in ISR handlers, which would cause stack overflow
 * or scheduler panic.
 * ========================================================= */
#define VK_ASSERT_NOT_ISR()                                                    \
    VK_ASSERT_FATAL(xPortInIsrContext() == 0,                                  \
                    "Called from ISR context — use FromISR variants")

/* =========================================================
 * IN-ISR — function must be called from interrupt context
 * ========================================================= */
#define VK_ASSERT_IN_ISR()                                                     \
    VK_ASSERT_FATAL(xPortInIsrContext() != 0,                                  \
                    "Must be called from ISR context")

/* =========================================================
 * PSRAM POINTER — pointer must reside in external PSRAM
 *
 * Ownership invariant: PSRAM chunk pool buffers must always
 * point into the pre-allocated PSRAM region. Warn (not fatal)
 * because some platforms may map PSRAM differently.
 * ========================================================= */
#define VK_ASSERT_PSRAM_PTR(ptr)                                               \
    VK_ASSERT_WARN(esp_ptr_external_ram(ptr),                                  \
                   #ptr " (%p) is not in PSRAM", (void *)(ptr))

/* =========================================================
 * FSM TRANSITION — validate legal state transition
 *
 * Usage:
 *   VK_ASSERT_FSM(new_state == IDLE || new_state == ARMED,
 *                 "audio FSM: illegal transition from IDLE to %d", new_state);
 * ========================================================= */
#define VK_ASSERT_FSM(condition, ...)                                          \
    VK_ASSERT_WARN(condition, "FSM: " __VA_ARGS__)

/* =========================================================
 * TASK NAME — current task must have expected name
 *
 * Warn only: useful for ownership validation in debug builds.
 * ========================================================= */
#define VK_ASSERT_TASK_NAME(expected_name)                                     \
    VK_ASSERT_WARN(strcmp(pcTaskGetName(NULL), (expected_name)) == 0,          \
                   "Expected task '%s', running in '%s'",                      \
                   (expected_name), pcTaskGetName(NULL))

/* =========================================================
 * RANGE CHECK — value within [min, max]
 * ========================================================= */
#define VK_ASSERT_RANGE(val, min, max)                                         \
    VK_ASSERT_WARN((val) >= (min) && (val) <= (max),                           \
                   #val " (%lld) out of range [%lld, %lld]",                   \
                   (long long)(val), (long long)(min), (long long)(max))

/* =========================================================
 * REACHABLE — marks code that must never execute
 *
 * Use in switch-default or after exhaustive if-else chains.
 * ========================================================= */
#define VK_UNREACHABLE(...)                                                    \
    do {                                                                       \
        ESP_LOGE(_VK_ASSERT_TAG,                                               \
                 "UNREACHABLE @ %s:%d: " __VA_ARGS__, __FILE__, __LINE__);     \
        esp_restart();                                                         \
    } while (0)

#else /* VK_ASSERT_DISABLE — strip all assertions */

#define VK_ASSERT_FATAL(c, ...)     do {} while (0)
#define VK_ASSERT_WARN(c, ...)      do {} while (0)
#define VK_ASSERT_NOT_NULL(p)       do {} while (0)
#define VK_ASSERT_QUEUE(q)          do {} while (0)
#define VK_ASSERT_NOT_ISR()         do {} while (0)
#define VK_ASSERT_IN_ISR()          do {} while (0)
#define VK_ASSERT_PSRAM_PTR(p)      do {} while (0)
#define VK_ASSERT_FSM(c, ...)       do {} while (0)
#define VK_ASSERT_TASK_NAME(n)      do {} while (0)
#define VK_ASSERT_RANGE(v, mn, mx)  do {} while (0)
#define VK_UNREACHABLE(...)         do {} while (0)

#endif /* VK_ASSERT_DISABLE */
