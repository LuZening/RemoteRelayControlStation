

#include "my_network.h"

#include "esp_wifi.h"
#include "nvs.h"
#define NVS_NAMESPACE "nvs"
nvs_handle my_nvs_handle;
#define NET_CONF_KEY "netconf"
#include "esp_log.h"
#define TAG "network"
#include "sdkconfig.h"
#include "freertos/event_groups.h"

/*** connection status Variables BEGIN ***/
WiFiConnType_t WiFiConnType = WIFI_NOT_CONNECTED;
char sSSIDConn[32] = {0};
esp_ip_addr_t ipWiFi = {.type = ESP_IPADDR_TYPE_V4}; // type: IPv4 or IPv6, by default AF_INET(IPv4), AF_INET6 if APv6

bool isEthernetConn;
esp_ip_addr_t ipEthernet = {.type = ESP_IPADDR_TYPE_V4}; // type: IPv4 or IPv6, by default AF_INET(IPv4), AF_INET6 if APv6
/*** connection status Variables END ***/

const char my_hostname[16] = "remote_relay";
/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/*  the global init of network both WiFi and ETH */
// call first
void init_network()
{
    // Initialize TCP/IP network interface (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

/* *************  ETHERNET CONFIG BEGIN ************** */
esp_eth_handle_t eth_handle = NULL;

eth_mac_clock_config_t clock_config = {
    .rmii.clock_mode = EMAC_CLK_OUT,
    .rmii.clock_gpio = EMAC_CLK_OUT_180_GPIO // 17
}; /*!< EMAC Interface clock configuration */


/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGD(TAG, "Ethernet Link Up");
        ESP_LOGD(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        isEthernetConn = true;
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "Ethernet Link Down");
        isEthernetConn = false;
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGD(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGD(TAG, "Ethernet Stopped");
        isEthernetConn = false;
        break;
    default:
        break;
    }
}


/** Event handler for IP_EVENT_ETH_GOT_IP */
static void eth_got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGD(TAG, "Ethernet Got IP Address");
    ESP_LOGD(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    // support IPv4 only,
    // TODO: support IPv6 in Ethernet 
    ipEthernet.u_addr.ip4 = ip_info->ip;
    ESP_LOGD(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGD(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));

}

void send_hard_reset_to_phy()
{

    gpio_set_level(GPIO_ETHERNET_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(GPIO_ETHERNET_RST, 1);
}

bool init_ethernet()
{
    bool r = true;
    isEthernetConn = false;
    // send reset signal
    gpio_set_direction(GPIO_ETHERNET_RST, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(GPIO_ETHERNET_RST, GPIO_PULLUP_ONLY);
    ESP_LOGD(TAG, "Hard reset Ethernet PHY...");
    send_hard_reset_to_phy();
    // 
    memset(&ipEthernet.u_addr, 0, sizeof(esp_ip6_addr_t));
    ipEthernet.type = AF_INET;
    // Initialize TCP/IP network interface (should be called only once in application)
    // ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    // ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create new default instance of esp-netif for Ethernet
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);

    // Init MAC and PHY configs to default
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    // PHY auto detect physical address
    phy_config.phy_addr = -1;
    // PHY do not use hardware reset
    phy_config.reset_gpio_num = -1;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.smi_mdc_gpio_num = GPIO_ETHERNET_MDC;
    mac_config.smi_mdio_gpio_num = GPIO_ETHERNET_MDIO;
    // MAC use GPIO17 and Output 180o reversed clock signal to EMAC 
    mac_config.clock_config = clock_config;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));
    /* attach Ethernet driver to TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_got_ip_event_handler, NULL));

    // start eth
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    return r;
}


bool ethernet_stop()
{
    bool r = true;
    /* start Ethernet driver state machine */
    ESP_ERROR_CHECK(esp_eth_stop(eth_handle));
    // CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET
    return r;
}


/* *************  ETHERNET CONFIG END ************** */



/* *************  WIFI CONFIG BEGIN ************** */
static int nWiFiRetry = 0;
static EventGroupHandle_t evtgrpWiFi;
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) 
    {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGD(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) 
    {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGD(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        esp_wifi_connect();
        nWiFiRetry = 0;
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        if (nWiFiRetry < 3) {
            esp_wifi_connect();
            nWiFiRetry++;
            ESP_LOGD(TAG, "retry to connect to the AP");
        } 
        else 
        {
            // record connection status
            WiFiConnType = WIFI_NOT_CONNECTED;
            memset(&ipWiFi.u_addr, 0, sizeof(esp_ip6_addr_t));
            xEventGroupSetBits(evtgrpWiFi, WIFI_FAIL_BIT);
            nWiFiRetry = 0;
        }
        ESP_LOGD(TAG,"connect to the AP fail");
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGD(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        nWiFiRetry = 0;
        // record connection status
        ipWiFi.u_addr.ip4 = event->ip_info.ip;
        xEventGroupSetBits(evtgrpWiFi, WIFI_CONNECTED_BIT);
    }
}

bool init_wifi(bool asAP, const char* ssid, const char* passwd)
{
    bool r =false;
    WiFiConnType = WIFI_NOT_CONNECTED;
    *sSSIDConn = 0;
    memset(&ipWiFi.u_addr, 0 ,sizeof(esp_ip6_addr_t));
    // Try to get WiFi configuration from NVS
    // ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH)); // WIFI_STORAGE_FLASH or WIFI_STORAGE_RAM

    if (asAP)
    {
        esp_netif_t* pAP = esp_netif_create_default_wifi_ap();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            NULL));

        wifi_config_t wifi_config = {
            .ap = {
                // .ssid = ssid,
                // .ssid_len = strlen(ssid),
                .channel = DEFAULT_WIFI_AP_CHANNEL,
                // .password = passwd,
                .max_connection = 4,
                .authmode = ((passwd != NULL)?(WIFI_AUTH_WPA2_PSK):(WIFI_AUTH_OPEN))
            },
        };
        // ssid and password
        if(ssid)
        {
            strncpy((char*)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid)-1);
            wifi_config.ap.ssid_len = strlen(ssid);
        }
        if(passwd)
        {
            strncpy((char*)wifi_config.ap.password, passwd, sizeof(wifi_config.ap.password)-1);
        }
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
        ESP_ERROR_CHECK( esp_wifi_start() );
        // record connection status
        WiFiConnType = WIFI_CONN_TYPE_AP;
        strncpy(sSSIDConn, (char*)wifi_config.ap.ssid, wifi_config.ap.ssid_len);
        sSSIDConn[wifi_config.ap.ssid_len] = 0;
        ipWiFi.type = ESP_IPADDR_TYPE_V4;
        ipWiFi.u_addr.ip4 = _g_esp_netif_soft_ap_ip.ip; // soft AP has deault ip settings
        r = true;
    }
    else // start WiFi as STA mode
    {
        WiFiConnType = WIFI_NOT_CONNECTED;
        evtgrpWiFi = xEventGroupCreate();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            &instance_any_id));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                    IP_EVENT_STA_GOT_IP,
                                                    &wifi_event_handler,
                                                    NULL,
                                                    &instance_got_ip));
        wifi_config_t wifi_config = {
            .sta = {
                // .ssid = ssid,
                // .password = passwd,
                /* Setting a password implies station will connect to all security modes including WEP/WPA.
                * However these modes are deprecated and not advisable to be used. Incase your Access point
                * doesn't support WPA2, these mode can be enabled by commenting below line */
                .threshold.authmode = ((passwd != NULL)?(WIFI_AUTH_WPA2_PSK):(WIFI_AUTH_OPEN)),
                
            },
        };
        // ssid and password
        if(ssid)
        {
            strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid)-1);
            ESP_LOGD(TAG, "trying to connect to: SSID = %s", ssid);
        }
        if(passwd)
        {
            strncpy((char*)wifi_config.sta.password, passwd, sizeof(wifi_config.sta.password)-1);
            ESP_LOGD(TAG, "trying to connect to: passwd = %s", passwd);
        }
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK( esp_wifi_start() );
        /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
        * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
        EventBits_t bits = xEventGroupWaitBits(evtgrpWiFi,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                pdFALSE,
                pdFALSE,
                pdTICKS_TO_MS(10000));

        /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
        * happened. */
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGD(TAG, "connected to ap SSID:%s",
                    ssid);
            strncpy(sSSIDConn, ssid, sizeof(sSSIDConn) - 1);
            WiFiConnType = WIFI_CONN_TYPE_STA;
            r = true;
        } else {
            ESP_LOGW(TAG, "Failed to connect to SSID:%s",
                    ssid);
        } 
        // else {
        //     ESP_LOGE(TAG, "UNEXPECTED EVENT");
        // }

        /* The event will not be processed after unregister */
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
        vEventGroupDelete(evtgrpWiFi);
        };
    return r;

}
/* *************  WIFI CONFIG END ************** */


