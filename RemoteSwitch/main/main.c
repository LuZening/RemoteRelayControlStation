#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "main.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include <sys/param.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"


#include "my_network.h"
#include "Config.h"
#include "relay.h"
#include "task_stepper.h"
#include "webserver.h"
#include "webclient.h"

#define tag "main"
EventGroupHandle_t evtgrpMain;

char ssid[32];
char pass[32];

extern const int RELAY_TYPES[N_RELAYS] = {RELAY_TYPE_LATCHING};
extern const int PIN_RELAYS_A[N_RELAYS] = {6};
extern const int PIN_RELAYS_B[N_RELAYS] = {7};
Relay_t Relays[N_RELAYS];

SemaphoreHandle_t* mtxRelayTime = NULL;
uint64_t RelayTimemsRemain[N_RELAYS];


void setup_relays()
{
    for(int i = 0; i < N_RELAYS; ++i)
    {
        Relay_t* p = &Relays[i];
        relay_init(p, RELAY_TYPES[i], PIN_RELAYS_A[i], PIN_RELAYS_B[i], true );
    }
}

void set_relay_state(int relayno, int state)
{
    // TODO:
    if(relayno <= 0) return;
    Relay_t* p = &Relays[relayno-1];
    relay_switch(p, state);
}

static void relay_timer_callback(void* arg)
{
    Relay_t* p = (Relay_t*)arg;
    int64_t time_since_boot = esp_timer_get_time();
    // ESP_LOGI(tag, "One-shot timer called, time since boot: %lld us", time_since_boot);
    relay_switch(p, 0); // turn off the relay

}

void set_timed_relay_state(int relayno, int state, uint32_t timems)
{
    if(relayno <= 0) return;
    Relay_t* p = &Relays[relayno-1];
    // TODO
    xSemaphoreTake(mtxRelayTime, portMAX_DELAY);
    RelayTimemsRemain[relayno-1] = timems;
    set_relay_state(relayno, 1);
    xSemaphoreGive(mtxRelayTime);

}

void cancel_timed_relay(int relayno, int state)
{
    if(relayno <= 0) return;
    Relay_t* p = &Relays[relayno-1];
    // TODO
    xSemaphoreTake(mtxRelayTime, portMAX_DELAY);
    RelayTimemsRemain[relayno-1] = 0;
    set_relay_state(relayno, 0);
    xSemaphoreGive(mtxRelayTime);

}







static void setup()
{
    setup_relays();
    // load config
    load_config(&cfg);
    // config invalid, start as AP
    bool initConfig = false;
    if(!config_check_valid(&cfg))
    {
        ESP_LOGW(tag, "Config not found");
        init_config(&cfg);
        initConfig = true;
        ESP_LOGD(tag, "Config initialized");
    }
    // init WiFi
    init_network();
    // load config
    load_config(&cfg);
    bool initConfig = false;
    if(!config_check_valid(&cfg))
    {
        
        ESP_LOGW(tag, "Config not found");
        init_config(&cfg);
        initConfig = true;
        ESP_LOGW(tag, "Config initialized");
    }
    // TODO: use securied nvs
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    nvs_handle_t hNVS;
    ret = nvs_open("storage", NVS_READWRITE, &hNVS);
    ESP_ERROR_CHECK(ret);
    size_t lenSSID, lenPass; 
    bool isWiFiParamsFoundInNVS = false;
    ret = nvs_get_str(hNVS, "WiFi_SSID", ssid, &lenSSID);
    if(ret == ESP_OK)
    {
        ssid[lenSSID] = 0;
        ret = nvs_get_str(hNVS, "WiFi_PASSWORD", pass, &lenPass);
        if(ret == ESP_OK)
        {
            pass[lenPass] = 0;
            isWiFiParamsFoundInNVS = true;
        }
        else
        {
            ESP_LOGW("WiFi", "Password not found in NVS");   
        }
    }
    else
    {
        ESP_LOGW("WiFi", "SSID not found in NVS");   
    }
    nvs_close(hNVS);
    
    if(isWiFiParamsFoundInNVS)
        init_wifi(false, ssid, pass); // start as STA
    else
        init_wifi(true, "RemoteRelay", NULL); // start as AP
    // initalize camera
    setup_camera();
    //
    init_webserver();
}

static void start_tasks()
{
    mqtt_app_start();
    xTaskCreate(task_relay, "task_relay", 1024, NULL, 4, NULL);
    xTaskCreate(task_motor, "task_motor", 2048, NULL, 5, NULL);
}



void task_relay(void* args)
{
    /* setup */
    const uint32_t stepms = 100;
    mtxRelayTime = xSemaphoreCreateMutex();
    for(int i = 0; i < N_RELAYS; ++i)
    {
        RelayTimemsRemain[i] = 0;
    }
    /* */
    TickType_t tickPrevWake = xTaskGetTickCount();
    while(1)
    {
        xSemaphoreTake(mtxRelayTime, portMAX_DELAY);
        for(int i = 0 ; i < N_RELAYS; ++i)
        {
            if(RelayTimemsRemain[i] > 0)
            {
                if(RelayTimemsRemain[i] <= stepms)
                {
                    RelayTimemsRemain[i] = 0;
                    set_relay_state(i+1, 0);
                }
                else
                {
                    RelayTimemsRemain[i] -= stepms;
                }
            } 
        }
        xSemaphoreGive(mtxRelayTime);
        vTaskDelayUntil(&tickPrevWake, pdMS_TO_TICKS(stepms));

    }
}


void reset_async()
{
    xEventGroupSetBits(evtgrpMain, SIGNAL_RESET);
}

void write_to_EEPROM_async()
{
    xEventGroupSetBits(evtgrpMain, SIGNAL_SAVE_CONFIG);
}

void app_main(void)
{


    // ESP_ERROR_CHECK(nvs_flash_init());
    // ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());

    setup();
    start_tasks();
    
    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    // TODO: init webserver
    while(1)
    {
        EventBits_t bits = xEventGroupWaitBits(evtgrpMain, 0b11, pdTRUE, pdFALSE, portMAX_DELAY);
        if((bits & SIGNAL_SAVE_CONFIG) > 0)  // save config on EEPROM
        {
            ESP_LOGD(tag, "Writing EEPROM...");
            // taskENTER_CRITICAL(&muxCritical);
            save_config(&cfg);
            // taskEXIT_CRITICAL(&muxCritical);
            ESP_LOGD(tag, "Writing EEPROM DONE!");
        }
        if((bits & SIGNAL_RESET) > 0) // reset
        {
            ESP_LOGD(tag, "received request for RESET...");
            abort();
        }
    }
    
}