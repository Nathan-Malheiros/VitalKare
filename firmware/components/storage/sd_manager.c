/**
 * @file sd_manager.c
 * @brief Gerenciamento do cartão SD via SDMMC (1-bit mode, ESP-IDF 5.x)
 */

#include "sd_manager.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "../../config/hardware_config.h"
#include <sys/stat.h>
#include <sys/unistd.h>
#include <string.h>

static const char *TAG = "SD";

static sdmmc_card_t *s_card   = NULL;
static bool          s_mounted = false;

esp_err_t sd_manager_init(void)
{
    if (s_mounted) return ESP_OK;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 8,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = HW_SD_FREQ_HZ / 1000;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk   = HW_SD_CLK_GPIO;
    slot.cmd   = HW_SD_CMD_GPIO;
    slot.d0    = HW_SD_D0_GPIO;
    slot.width = 1;   /* 1-bit mode */
    slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(HW_SD_MOUNT_POINT, &host,
                                              &slot, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Falha ao montar FAT. Formate o cartão SD como FAT32.");
        } else {
            ESP_LOGE(TAG, "SD não encontrado: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD montado em %s — %.1f MB",
             HW_SD_MOUNT_POINT,
             (float)((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) / (1024 * 1024));
    return ESP_OK;
}

bool sd_manager_is_mounted(void) { return s_mounted; }

esp_err_t sd_manager_get_free_mb(uint32_t *out_free_mb)
{
    if (!s_mounted || out_free_mb == NULL) return ESP_ERR_INVALID_STATE;
    FATFS *fs;
    DWORD fre_clust;
    if (f_getfree("0:", &fre_clust, &fs) != FR_OK) return ESP_FAIL;
    *out_free_mb = (uint32_t)((uint64_t)fre_clust * fs->csize * 512 / (1024 * 1024));
    return ESP_OK;
}

esp_err_t sd_manager_mkdir(const char *path)
{
    if (!s_mounted || path == NULL) return ESP_ERR_INVALID_STATE;
    struct stat st;
    if (stat(path, &st) == 0) return ESP_OK;  /* Já existe */
    if (mkdir(path, 0755) != 0) {
        ESP_LOGW(TAG, "mkdir(%s) falhou", path);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Diretório criado: %s", path);
    return ESP_OK;
}
