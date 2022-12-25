#pragma once

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// #include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"


#include "esp_err.h"
#include "esp_event.h"


#define DEFAULT_WIFI_AP_SSID "RemoteRelay"
#define DEFAULT_WIFI_AP_CHANNEL 1

/*** connection status Variables BEGIN ***/
typedef enum {
    WIFI_NOT_CONNECTED = 0,
    WIFI_CONN_TYPE_AP,
    WIFI_CONN_TYPE_STA 
} WiFiConnType_t;

extern WiFiConnType_t WiFiConnType;
extern char sSSIDConn[32];
extern esp_ip_addr_t ipWiFi; // type: IPv4 or IPv6


extern bool isEthernetConn;
extern esp_ip_addr_t ipEthernet; // type: IPv4 or IPv6
/*** connection status Variables END ***/







// call first
void init_network();
/* *************  ETHERNET CONFIG BEGIN ************** */
extern esp_eth_handle_t eth_handle;

#define GPIO_ETHERNET_MDC 23
#define GPIO_ETHERNET_MDIO 18
#define GPIO_ETHERNET_RST 0


extern eth_mac_clock_config_t clock_config;

bool init_ethernet();
bool ethernet_start();
bool ethernet_stop();

/* *************  ETHERNET CONFIG END ************** */




/* *************  WIFI CONFIG BEGIN ************** */
bool init_wifi(bool asAP, const char* ssid, const char* passwd);
/* *************  WIFI CONFIG BEGIN ************** */

