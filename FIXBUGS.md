# VitalKare — Bug Fixes & Audit Corrections

All bugs identified and fixed during the hardware integration audit and simulation mode implementation session.

---

## 1. OLED Display — Multi-byte I2C Command Fragmentation

**File:** `firmware/components/display/oled_driver.c`  
**Severity:** Critical — display produced no output at all

**Root cause:**  
The SSD1306 uses a stateful command parser. Commands like `Set Column Address (0x21)` take two parameter bytes that must arrive in the same I2C transaction. The original driver called `ssd1306_cmd()` once per byte, generating a separate I2C START/STOP for every byte. Many SSD1306 clones reset their command state machine on each STOP condition, so the parameter bytes were received as standalone commands:

- `0x7F` (column end = 127) → interpreted as **"Set Display Start Line = 63"**, shifting all content 63 rows off-screen
- `0x00` (parameter for `0x20` horizontal addressing mode) → interpreted as **"Set Lower Column Start Address"**, leaving the controller in default page-addressing mode so the framebuffer flush had no effect

**Fix:**  
Added `ssd1306_cmdbuf(const uint8_t *cmds, size_t n)` helper that sends up to 8 command bytes in a single I2C transaction. Rewrote `oled_init()` to use `ssd1306_cmdbuf()` for every multi-byte command:

```c
ret |= ssd1306_cmdbuf((uint8_t[]){0xD5, 0x80}, 2);   // Clock divide ratio
ret |= ssd1306_cmdbuf((uint8_t[]){0xA8, 0x3F}, 2);   // Mux ratio
ret |= ssd1306_cmdbuf((uint8_t[]){0xD3, 0x00}, 2);   // Display offset
ret |= ssd1306_cmdbuf((uint8_t[]){0x8D, 0x14}, 2);   // Charge pump
ret |= ssd1306_cmdbuf((uint8_t[]){0x20, 0x00}, 2);   // Horizontal addressing ← CRITICAL
ret |= ssd1306_cmdbuf((uint8_t[]){0xDA, 0x12}, 2);   // COM pins
ret |= ssd1306_cmdbuf((uint8_t[]){0x81, 0xCF}, 2);   // Contrast
ret |= ssd1306_cmdbuf((uint8_t[]){0xD9, 0xF1}, 2);   // Pre-charge period
ret |= ssd1306_cmdbuf((uint8_t[]){0xDB, 0x40}, 2);   // VCOMH deselect
```

Rewrote `oled_flush()` window setup:

```c
// Before (broken — 6 separate transactions):
ret  = ssd1306_cmd(0x21);
ret |= ssd1306_cmd(0);
ret |= ssd1306_cmd(127);
ret |= ssd1306_cmd(0x22);
ret |= ssd1306_cmd(0);
ret |= ssd1306_cmd(7);

// After (correct — 2 atomic transactions):
ret  = ssd1306_cmdbuf((uint8_t[]){0x21, 0x00, 0x7F}, 3);  // col 0→127
ret |= ssd1306_cmdbuf((uint8_t[]){0x22, 0x00, 0x07}, 3);  // page 0→7
```

**Diagnostic logging added:**
- `[DISPLAY] OLED initialization started`
- `[DISPLAY] OLED detected at address 0x3C`
- `[DISPLAY] Display flush successful (N bytes)`
- `[ERROR] Display initialization failed: <reason>`

---

## 2. Oximeter Task — Missing Watchdog Heartbeat

**File:** `firmware/components/sensors/max30102.c`  
**Severity:** High — task monitor would flag TASK_ID_OXIMETER as stalled after 15 s and trigger a reboot

**Root cause:**  
`task_oximeter_poll` never called `task_monitor_heartbeat(TASK_ID_OXIMETER)`, so the task monitor's stall timer for slot 6 was never reset. Under the configured thresholds (WARN=5 s, FAULT=15 s, REBOOT=30 s) the watchdog would eventually reboot the device.

**Fix:**

```c
// Added include:
#include "../core/task_monitor.h"

// Added at the top of the poll loop:
for (;;) {
    vTaskDelay(pdMS_TO_TICKS(APP_OXIMETER_POLL_INTERVAL_MS));
    task_monitor_heartbeat(TASK_ID_OXIMETER);   // ← added
    ...
}
```

---

## 3. Server — `finger_detected` Type Mismatch

**File:** `server/vk_proto.py`  
**Severity:** High — "no finger detected" state was silently ignored by the frontend

**Root cause:**  
`parse_telemetry_oximeter()` returned `finger_detected: bool(finger)`, which Python serialises to JSON `true` / `false`. The TypeScript device store typed the field as `number | null`, and the frontend checked `device.finger_detected === 0` (strict equality). In JavaScript, `false === 0` is `false`, so the "finger absent" condition was never triggered.

**Fix:**

```python
# Before:
"finger_detected": bool(finger),

# After:
"finger_detected": int(finger),   # 0 or 1 — matches TypeScript number type
```

---

## 4. I2C Bus — Hardcoded GPIO Pin Numbers

**File:** `firmware/components/i2c_bus/i2c_bus.c`  
**Severity:** Medium — maintenance risk; pin change in hardware_config.h would not propagate

**Root cause:**  
The I2C bus driver defined its own `#define I2C_BUS_SDA_GPIO 14` and `#define I2C_BUS_SCL_GPIO 21` instead of referencing the canonical values in `hardware_config.h`. Any KiiraV2 hardware revision changing these pins would require updating two files.

**Fix:**

```c
// Added include:
#include "../../config/hardware_config.h"

// Before:
#define I2C_BUS_SDA_GPIO    14
#define I2C_BUS_SCL_GPIO    21

// After:
#define I2C_BUS_SDA_GPIO    HW_OLED_SDA_GPIO
#define I2C_BUS_SCL_GPIO    HW_OLED_SCL_GPIO
```

---

## 5. Display Subsystem Health Not Reported

**File:** `firmware/components/display/ui_renderer.c`  
**Severity:** Medium — system telemetry always showed display health as `false` (never updated after boot)

**Root cause:**  
`system_manager.c` initialised `SUBSYS_DISPLAY` to `false` at boot with a comment noting that `task_display_update` was responsible for reporting the real status after `oled_init()`. However, `task_display_update` never called `system_manager_report_subsystem()`, so the dashboard always received `display_ok = false` regardless of actual display state.

**Fix:**

```c
// Added include:
#include "../core/system_manager.h"

// In task_display_update(), after oled_init():
if (oled_init(ctx) != ESP_OK) {
    ESP_LOGE(TAG, "OLED init falhou na task — task encerrada");
    system_manager_report_subsystem(SUBSYS_DISPLAY, false);   // ← added
    vTaskDelete(NULL);
    return;
}
system_manager_report_subsystem(SUBSYS_DISPLAY, true);        // ← added
```

---

## 6. Sensor Init Logging — Missing `[SENSOR]` Audit Prefix

**Files:**  
- `firmware/components/sensors/mpu6050.c`  
- `firmware/components/sensors/hdc1080.c`  
- `firmware/components/sensors/max30102.c`  

**Severity:** Low — operational only; no functional impact

**Root cause:**  
Sensor init success messages did not carry the `[SENSOR]` prefix required by the hardware audit logging standard, making log-based triage harder.

**Fix:**

| File | Before | After |
|------|--------|-------|
| `mpu6050.c` | `MPU6050 inicializado (addr=0x68)` | `[SENSOR] Accelerometer initialized (MPU6050 addr=0x68)` |
| `hdc1080.c` | `HDC1080 inicializado (I2C addr=0x40)` | `[SENSOR] Environment sensor initialized (HDC1080 addr=0x40)` |
| `max30102.c` | `MAX30102 inicializado (addr=0x57)` | `[SENSOR] Oximeter initialized (MAX30102 addr=0x57)` |

---

## 7. Simulation Mode — New Component for ESP32-S3 DevKit

**Files created:**  
- `firmware/components/simulation/sim_sensors.h`  
- `firmware/components/simulation/sim_sensors.c`  
- `firmware/components/simulation/CMakeLists.txt`  

**Files modified:**  
- `firmware/config/board_profile.h`  
- `firmware/main/main.c`  
- `firmware/main/CMakeLists.txt`  

**Context:**  
The DevKit board profile had no way to run the full system stack without KiiraV2 hardware. A clean simulation layer was introduced so Wi-Fi, TCP, protocol, and dashboard communication could be validated without sensors or display attached.

**What was implemented:**

- New `VK_SIMULATION_MODE` preprocessor flag — automatically `1` when `VK_BOARD_PROFILE == VK_BOARD_PROFILE_ESP32S3_DEVKIT`, always `0` on KiiraV2
- New `VK_CAP_SIMULATION (0x40000000u)` capability bit published in the handshake
- `sim_sensors_init()` + `task_sim_telemetry()` task that emits realistic sinusoidal telemetry without touching any hardware driver:
  - `PKT_TELEMETRY_OXIMETER` every 5 s — BPM 65–95, SpO2 96–100%, confidence, finger_detected=1
  - `PKT_TELEMETRY_ENV` every 10 s — temp 22–25 °C, humidity 45–65%, lux 100–600, noise
  - Fires `EVT_BTN_A_PRESSED` at t=30 s then every 5 min (simulates nurse call)
  - Feeds `task_monitor_heartbeat()` for both TASK_ID_SENSOR and TASK_ID_OXIMETER
- The entire `sim_sensors.c` body is wrapped in `#if VK_SIMULATION_MODE` — zero footprint on KiiraV2 builds
- Mandatory boot log lines emitted on DevKit:
  ```
  [SIMULATION] Running in simulated hardware mode
  [SIMULATION] Sensor providers replaced with virtual data
  [SIMULATION] KiiraV2 hardware access disabled
  ```

---

## Summary Table

| # | File | Type | Impact |
|---|------|------|--------|
| 1 | `display/oled_driver.c` | Bug fix | Display was completely blank |
| 2 | `sensors/max30102.c` | Bug fix | Watchdog reboot every ~30 s |
| 3 | `server/vk_proto.py` | Bug fix | Finger-absent state never shown on dashboard |
| 4 | `i2c_bus/i2c_bus.c` | Maintenance | Hardcoded pins diverge from hardware_config.h |
| 5 | `display/ui_renderer.c` | Bug fix | Display health always reported as failed |
| 6 | `sensors/mpu6050.c`, `hdc1080.c`, `max30102.c` | Logging | Audit prefix missing |
| 7 | `components/simulation/*`, `main/main.c`, `board_profile.h` | Feature | DevKit simulation mode |
