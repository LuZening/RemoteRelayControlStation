#include "Config.h"


#include "esp_log.h"
#include <string.h>
#include "main.h"
#include "string.h"
// #include "AT24C.h"
#include "nvs_flash.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "mbedtls/base64.h"
#define tag "config"

Config cfg;

byte page_active;

#ifdef USE_MUTEX_ON_CFG
SemaphoreHandle_t mtxConfig = NULL;
#endif

bool isModified = false;
// use '.' to indicate the positon in an array
// for '.' is well supported in HTML URL encoding
// either '()' or '[]' will be converted in HTML URL
const config_var_map_t configNameMapper[] = {
    {"s_name", &(cfg.s_name), CONFIG_VAR_BYTESTRING},
    {"device_id", &(cfg.device_id), CONFIG_VAR_BYTESTRING},
    {"enable_upload_to_server", &(cfg.enable_upload_to_server), CONFIG_VAR_U8},
    {"upload_dest_URL", &(cfg.upload_dest_URL), CONFIG_VAR_BYTESTRING},
    {"upload_password", &(cfg.upload_password), CONFIG_VAR_BYTESTRING},
    {"mqtt_broker_URL", &(cfg.mqtt_broker_URL), CONFIG_VAR_BYTESTRING},
    // {"WiFi_SSID", &(cfg.WiFi_SSID), CONFIG_VAR_BYTESTRING},
    // {"WiFi_password", &(cfg.WiFi_password), CONFIG_VAR_BYTESTRING},
};


void init_config(struct Config *p)
{
    #ifdef USE_MUTEX_ON_CFG
    if(mtxConfig == NULL) mtxConfig = xSemaphoreCreateMutex();
    xSemaphoreTake(mtxConfig, portMAX_DELAY);
    #endif
    /*TODO*/
    // valid string
    strncpy(cfg.sValid, VALID_STRING, sizeof(cfg.sValid));
    // device_id, use MAC address as the generator of the name
    uint8_t mac[8];
    esp_efuse_mac_get_default(mac);
    sprintf(cfg.device_id, DEFAULT_ID_PREFIX "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // name, use device id as default
    strncpy(cfg.s_name, cfg.device_id, sizeof(cfg.s_name));
    
    cfg.enable_upload_to_server = true;
    memset(cfg.upload_dest_URL, 0, sizeof(cfg.upload_dest_URL));
    memset(cfg.upload_password, 0, sizeof(cfg.upload_password));

    memset(cfg.mqtt_broker_URL, 0, sizeof(cfg.mqtt_broker_URL));




    #ifdef USE_MUTEX_ON_CFG
    xSemaphoreGive(mtxConfig);
    #endif
}


void push_config_to_volatile_variables(struct Config* p)
{
    int i;
    #ifdef USE_MUTEX_ON_CFG
    xSemaphoreTake(mtxConfig, portMAX_DELAY);
    #endif
    #ifdef USE_MUTEX_ON_CFG
    xSemaphoreGive(mtxConfig);
    #endif
}

// flash wear levelling handle
static wl_handle_t wlHandle = WL_INVALID_HANDLE;
void load_config(union ConfigWriteBlock* p_cfg)
{
    #ifdef USE_MUTEX_ON_CFG
    if(mtxConfig == NULL) mtxConfig = xSemaphoreCreateMutex();
    #endif
    #ifdef USE_EEPROM
    EEPROM_ReadRange(&eeprom, ADDR_CONFIG_BEGIN, p_cfg->bytes, sizeof(Config));
    #else
    // Use FatFS by default
    esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 4,
        .format_if_mount_failed = true,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount(FS_BASE_PATH_CONFIG, "storage", &mount_config, &wlHandle);
    if (err != ESP_OK) {
        ESP_LOGE(tag, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }
    ESP_LOGD(tag, "Opening file");
    FILE *f = fopen(FS_PATH_CONFIG_FILE, "rb");
    if (f == NULL) {
        ESP_LOGE(tag, "Failed to open file %s for reading", FS_PATH_CONFIG_FILE);
    }
    else
    {
        // load file content to config variable
        ESP_LOGD(tag, "Read from file %s", FS_PATH_CONFIG_FILE);
        size_t n = fread(p_cfg->bytes, sizeof(Config), 1, f);
        fclose(f);
        ESP_LOGD(tag, "Read from file %s succeeded, %d bytes", FS_PATH_CONFIG_FILE, n);
        ESP_LOGD(tag, "cfg.validstring = %s", p_cfg->body.sValid);
    }
    esp_vfs_fat_spiflash_unmount(FS_BASE_PATH_CONFIG, wlHandle);    
    ESP_LOGD(tag, "FS unmounted");
    #endif

    // read_array_AT24C(pEEPROM, PAGE2ADDR(pEEPROM, PAGE_CONFIG), p_cfg->bytes, sizeof(ConfigWriteBlock));
    // ESP_LOGD("CONFIG", "ssid=%s, passwd=%s", p_cfg->body.s_ssid, p_cfg->body.s_password);
}


void save_config(union ConfigWriteBlock* p_cfg)
{
    #ifdef USE_MUTEX_ON_CFG
    xSemaphoreTake(mtxConfig, portMAX_DELAY);
    #endif

    #ifdef USE_EEPROM
    EEPROM_release_WP(&eeprom);
    // ACK_polling_AT24C(pEEPROM);
    // write_array_AT24C(pEEPROM, 1, p_cfg->bytes, sizeof(ConfigWriteBlock));
    
    EEPROM_WriteRange(&eeprom, ADDR_CONFIG_BEGIN, sizeof(Config), p_cfg->bytes);
    EEPROM_WP(&eeprom);
    #else
    // Use FatFS by default
    esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 4,
        .format_if_mount_failed = true,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount(FS_BASE_PATH_CONFIG, "storage", &mount_config, &wlHandle);
    if (err != ESP_OK) {
        ESP_LOGE(tag, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        goto SAVE_CONFIG_FAILED;
    }
    ESP_LOGD(tag, "Opening file");
    FILE *f = fopen(FS_PATH_CONFIG_FILE, "wb");
    if (f == NULL) {
        ESP_LOGE(tag, "Failed to open file %s for writing", FS_PATH_CONFIG_FILE);
    }
    else
    {
        // load file content to config variable
        ESP_LOGD(tag, "Dump to file %s", FS_PATH_CONFIG_FILE);
        ESP_LOGD(tag, "cfg.validstring = %s", p_cfg->body.sValid);
        size_t n = fwrite(p_cfg->bytes, sizeof(Config), 1, f);
        fclose(f);
        ESP_LOGD(tag, "Dump to file %s succeeded, %d bytes", FS_PATH_CONFIG_FILE, n);
    }
    esp_vfs_fat_spiflash_unmount(FS_BASE_PATH_CONFIG, wlHandle);    
    ESP_LOGD(tag, "FS unmounted");
    #endif


    isModified=false;
SAVE_CONFIG_FAILED:
    #ifdef USE_MUTEX_ON_CFG
    xSemaphoreGive(mtxConfig);
    #endif
    return;
}


bool config_check_valid(struct Config* p)
{
    uint8_t check = strncmp(p->sValid, VALID_STRING, sizeof(VALID_STRING)-1);
    return (check == 0);

}


bool set_config_variable_by_name(const char* name, const void* pV)
{
    const int N = (sizeof(configNameMapper) / sizeof(config_var_map_t));
    int i;
    bool found = false;
    // match the config entry by comparing names
    for(i = 0; i < N; ++i)
    {
        if(strcmp(configNameMapper[i].name, name) == 0)
        {
            found = true;
            break;
        }
    }
    if(found)
    {
        const config_var_map_t* pMap = &(configNameMapper[i]);
        #ifdef USE_MUTEX_ON_CFG
        xSemaphoreTake(mtxConfig, portMAX_DELAY);
        #endif
        switch (pMap->typ)
        {
        case CONFIG_VAR_U8:
            *(uint8_t*)(pMap->pV) = *(uint8_t*)pV;
            ESP_LOGD(tag, "set %s = %d", name, *(uint8_t*)pV);
            break;
        case CONFIG_VAR_I32:
            memcpy(pMap->pV, pV, sizeof(int));
            ESP_LOGD(tag, "set %s = %d", name, *(int32_t*)pV);
            break;
        case CONFIG_VAR_BYTESTRING:
            strncpy((char*)(pMap->pV), (char*)pV, 31);
            ESP_LOGD(tag, "set %s = %s", name, (char*)pV);
            break;
        default:
            break;
        }
        isModified = true;
        #ifdef USE_MUTEX_ON_CFG
        xSemaphoreGive(mtxConfig);
        #endif
    }
    return found;
}


bool get_if_config_modified()
{
    #ifdef USE_MUTEX_ON_CFG
    xSemaphoreTake(mtxConfig, portMAX_DELAY);
    #endif
    bool r = isModified;
    #ifdef USE_MUTEX_ON_CFG
    xSemaphoreGive(mtxConfig);
    #endif
    return r;
}


config_var_map_t* get_config_variable_mapper_item_by_name(const char* name)
{
    const int N = (sizeof(configNameMapper) / sizeof(config_var_map_t));
    int i;
    bool found = false;
    config_var_map_t* pFound = NULL;
    // match the config entry by comparing names
    for(i = 0; i < N; ++i)
    {
        if(strcmp(configNameMapper[i].name, name) == 0)
        {
            found = true;
            pFound = &(configNameMapper[i]);
            break;
        }
    }
    return pFound;
}

bool get_config_variable_by_name(const char* name, void* buf)
{
    const int N = (sizeof(configNameMapper) / sizeof(config_var_map_t));
    int i;
    bool found = false;
    // match the config entry by comparing names
    for(i = 0; i < N; ++i)
    {
        if(strcmp(configNameMapper[i].name, name) == 0)
        {
            found = true;
            break;
        }
    }
    if(found)
    {
        const config_var_map_t* pMap = &(configNameMapper[i]);
        #ifdef USE_MUTEX_ON_CFG
        xSemaphoreTake(mtxConfig, portMAX_DELAY);
        #endif
        switch (pMap->typ)
        {
        case CONFIG_VAR_U8:
            *(uint8_t*)buf = *(uint8_t*)(pMap);
            break;
        case CONFIG_VAR_I32:
            memcpy(buf, pMap->pV, sizeof(int));
            break;
        case CONFIG_VAR_BYTESTRING:
            strncpy((char*)buf, (char*)(pMap->pV), 31);
            break;
        }
        #ifdef USE_MUTEX_ON_CFG
        xSemaphoreGive(mtxConfig);
        #endif
    }
    return found;
}

int get_config_string(char* buf, int lenbuf)
{
    const int N = (sizeof(configNameMapper) / sizeof(config_var_map_t));
    int i;
    char* p = buf;
    int nWritten = 0;
    int nW = 0;
    if (lenbuf <= 1)
        return 0;
    #ifdef USE_MUTEX_ON_CFG
    xSemaphoreTake(mtxConfig, portMAX_DELAY);
    #endif
    for(i = 0; i < N; ++i)
    {
        config_var_map_t *pMap = &(configNameMapper[i]);
        if(nWritten >= lenbuf - 1)
            break;
        switch(pMap->typ)
        {
            case CONFIG_VAR_U8:
            nW = snprintf(p, lenbuf - nWritten - 1, 
                "%s=%d", pMap->name, *((uint8_t*)(pMap->pV)));
            break;
            case CONFIG_VAR_I32:
            nW = snprintf(p, lenbuf - nWritten - 1, 
                "%s=%d", pMap->name, *((int*)(pMap->pV)));
            break;
            case CONFIG_VAR_BYTESTRING:
            nW = snprintf(p, lenbuf - nWritten - 1, 
                "%s=%s", pMap->name, ((char*)(pMap->pV)));
            break;
        }
        nWritten += nW;
        p += nW;
        if(nWritten < lenbuf - 1)
        {
            if(i == N-1)
            {
                *p = '\n';
            }
            else
            {
                *p = '&';
            }
            nWritten++;
            p++;
        }
    }
    buf[nWritten] = 0; // terminate the string
    #ifdef USE_MUTEX_ON_CFG
    xSemaphoreGive(mtxConfig);
    #endif
    return nWritten;
}
