#pragma once

#include <stdbool.h>

#ifdef FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#define USE_MUTEX_ON_CFG 1
#endif

#define FS_BASE_PATH_CONFIG "/spiflash"
#define FS_PATH_CONFIG_FILE "/spiflash/cfg.bin"

#define VALID_STRING "REMOTERELAY"
#define DEFAULT_ID_PREFIX "RemoteRelay"
#define CONFIG_BYTESTRING_LEN 32
#define ADDR_CONFIG_BEGIN 0
#define ADDR_ACTIVE_STATIC
// #define ADDR_ACTIVE_DYNAMIC
#define ADDR_ACTIVE_BEGIN 1024
typedef unsigned char byte;



/* do not use float parameters */
// type of variables: uint8_t, int, char
struct Config
{
    // identity
    char sValid[16];
    // device_id
    char device_id[32];
    // name
    char s_name[64];
    // upload to server
    uint8_t enable_upload_to_server;
    // upload destination URL
    char upload_dest_URL[256];
    char upload_password[64];
    // mqtt
    char mqtt_broker_URL[256];
    // WiFi
    // char WiFi_SSID[CONFIG_BYTESTRING_LEN];
    // char WiFi_password[CONFIG_BYTESTRING_LEN];
    
}; // size of config is around 192Bytes

typedef struct Config Config;

extern struct Config cfg;



union ConfigWriteBlock
{
    struct Config body;
    byte bytes[sizeof(struct Config)];
};


extern union ConfigWriteBlock *p_cfg;

/* Config name->pointer mapper  */
typedef enum
{
    CONFIG_VAR_U8 = 0,
    CONFIG_VAR_I32,
    CONFIG_VAR_BYTESTRING
} config_var_type_t;
typedef struct
{
    const char* name;
    void* pV;
    config_var_type_t typ;
} config_var_map_t;

extern const config_var_map_t configNameMapper[];

#ifdef USE_MUTEX_ON_CFG
extern SemaphoreHandle_t mtxConfig;
#endif

void load_config(union ConfigWriteBlock* p_cfg);
// if config read from EEPROM is not 
void init_config(struct Config *p);


void save_config(union ConfigWriteBlock* p_cfg);
bool config_check_valid(struct Config* p);

// set the value of the config variable by name
bool set_config_variable_by_name(const char* name, const void* pV);
// get the value of the config variable by name
// @name the name of the config variable
// @buf the memory to store the value of the variable. If the variable's value is a string, the size of the buf must >= 32 
bool get_config_variable_by_name(const char* name, void* buf);
bool get_if_config_modified();
// get the pointer to the config variable by name
// return NULL if not found
config_var_map_t* get_config_variable_mapper_item_by_name(const char* name);

// Sync working objects, such as RotSensor and etc, with config variables
// call after config variables are updated
void push_config_to_volatile_variables(struct Config* p);


int get_config_string(char *buf, int lenbuf);