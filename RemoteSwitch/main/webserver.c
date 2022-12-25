#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "webserver.h"
#include "string.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "libesphttpd/httpd.h"
#include "libesphttpd/httpd-espfs.h"
#include "libesphttpd/cgiwifi.h"
#include "libesphttpd/cgiflash.h"
#include "libesphttpd/auth.h"
#include "libesphttpd/captdns.h"
#include "libesphttpd/cgiwebsocket.h"
#include "libesphttpd/httpd-freertos.h"
#include "libesphttpd/route.h"
#include "libesphttpd/httpd-espfs.h"
#include "esp_camera.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cam.h"

#include "relay.h"
#include "Config.h"
#define tag "webserver"

// static const char *espfs_image_bin = WEBSERVER_FS_ADDR_BEGIN;
static char connectionMemory[sizeof(RtosConnType) * MAX_CONNECTIONS];
static HttpdFreertosInstance httpdFreertosInstance;

// http string temporary write buffer
static char buf1[1500];
SemaphoreHandle_t mtxWebBuf1;
// only 1 Websocket sending request is allowed each time
SemaphoreHandle_t mtxWSSending;

bool set_wifi_params(const char* SSID, const char* password)
{
    // TODO: use securied nvs
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    nvs_handle_t hNVS;
    ret = nvs_open("storage", NVS_READWRITE, &hNVS);
    ESP_ERROR_CHECK(ret);
    if(SSID)
        nvs_set_str(hNVS, "WiFi_SSID", SSID);
    if(password)
        nvs_set_str(hNVS, "WiFi_PASSWORD", password);
    return true;
}


/* Webserver URL hander: /reset  */
static CgiStatus ICACHE_FLASH_ATTR onReset(HttpdConnData *connData)
{
    if (connData->isConnectionClosed) {
        return HTTPD_CGI_DONE;
    }
    /* Response BEGIN */
    write_to_EEPROM_async();
    httpdStartResponse(connData, 200);
    httpdHeader(connData, "Content-Type", "text/plain");
    httpdEndHeaders(connData);
    httpdSend(connData, "reset", -1);
    /* Response END */
    // TODO: send reset semaphore
    reset_async();
    return HTTPD_CGI_DONE;
}


/*
* @args SSID=&password=&etc
*/

static CgiStatus ICACHE_FLASH_ATTR onSetWiFi(HttpdConnData *connData)
{
    char sVal[32]; 
    size_t lenVal;
    bool isErr = false;
    char errmsg[128] = {0};
    if(connData->requestType == HTTPD_METHOD_POST)
    {
        lenVal = httpdFindArg(connData->post.buff, "SSID", sVal, sizeof(sVal));
        if(lenVal > 0)
        {
            set_wifi_params(sVal, NULL);
        }
        else
        {
            strcat(errmsg, "Missing argument: SSID");
            goto failed;
        }
        lenVal = httpdFindArg(connData->post.buff, "password", sVal, sizeof(sVal));
        if(lenVal > 0)
        {
            set_wifi_params(NULL, sVal);
        }
        else
        {
            strcat(errmsg, "Missing argument: password");
            goto failed;
        }
    }
    httpdStartResponse(connData, 200);
    httpdHeader(connData, "Content-Type", "text/plain");
    httpdEndHeaders(connData);
    httpdSend(connData, "done", -1);
    return HTTPD_CGI_DONE;
failed:
    httpdStartResponse(connData, 300);
    httpdHeader(connData, "Content-Type", "text/plain");
    httpdEndHeaders(connData);
    httpdSend(connData, errmsg, -1);
    return HTTPD_CGI_DONE;

}


static CgiStatus ICACHE_FLASH_ATTR onGetConfig(HttpdConnData *connData)
{
    if (connData->isConnectionClosed) {
        return HTTPD_CGI_DONE;
    }
    // avoid multiple write of buf1, to save memory
    xSemaphoreTake(mtxWebBuf1, portMAX_DELAY);
    get_config_string(buf1, sizeof(buf1));
    httpdStartResponse(connData, 200);
    httpdHeader(connData, "Content-Type", "text/plain");
    httpdEndHeaders(connData);
    httpdSend(connData, buf1, -1);
    xSemaphoreGive(mtxWebBuf1);
    return HTTPD_CGI_DONE;
}

static CgiStatus ICACHE_FLASH_ATTR onSetConfig(HttpdConnData *connData)
{
    if (connData->isConnectionClosed) {
        return HTTPD_CGI_DONE;
    }
    if(connData->requestType == HTTPD_METHOD_POST)
    {
        const int lenbuf = 256;
        char bufR[lenbuf];
        int nParsed = 0;
        char* pData = connData->post.buff;
        int nBytes = connData->post.buffLen;
        if(nBytes >= lenbuf) nBytes = lenbuf - 1;
        strncpy(bufR, pData, nBytes);
        bufR[lenbuf] = 0;
        int i = 0, j = 0, k = 0; // i : start of arg name, j : position of =, k : position of &
        while(i <= nBytes)
        {
            // pick up an arg
            while(k <= nBytes)
            {
                if(bufR[k] != '&' && bufR[k] != '\n' && bufR[k] != '\r')
                {
                    k++;
                    if(bufR[k] == '=') j = k;
                }
                else
                    break;
            }
            // judge if = appears after the argument name, and & happends after =
            // note: k can be equal to j, which happens when the argument value is blank
            if(j > i && k >= j)
            {
                // tokenize the argname and arg value
                bufR[j] = 0; bufR[k] = 0;
                const char* argname = &(bufR[i]);
                const char* argv = &(bufR[j+1]);
                // parse
                int val;
                bool isArgValueParsed = false;
                // locate the config variable by name, if failed return NULL
                config_var_map_t* pCfgItem = get_config_variable_mapper_item_by_name(argname);
                if (pCfgItem)
                {
                    switch(pCfgItem->typ)
                    {
                        case CONFIG_VAR_U8:
                            val = atoi(argv);
                            isArgValueParsed = true;
                            *(uint8_t*)(pCfgItem->pV) = val;
                            ESP_LOGD(tag, "Set config U8 %s = %d", argname, val);
                            break;
                        case CONFIG_VAR_I32:
                            val = atoi(argv);
                            isArgValueParsed = true;
                            *(int32_t*)(pCfgItem->pV) = val;
                            ESP_LOGD(tag, "Set config I32 %s = %d", argname, val);
                            break;
                        case CONFIG_VAR_BYTESTRING:
                            val = 0;
                            /* copy string to config */
                            while(val < (CONFIG_BYTESTRING_LEN-1) && argv[val] != 0)
                            {
                                *(uint8_t*)pCfgItem->pV = argv[val];
                                val++;
                            }
                            if(val < CONFIG_BYTESTRING_LEN - 1)
                            {
                                ((uint8_t*)(pCfgItem->pV))[val + 1] = 0;
                                isArgValueParsed = true;
                            }
                            else // buffer overflowed, input string is too long
                            {
                                ((uint8_t*)(pCfgItem->pV))[val] = 0;
                                isArgValueParsed = false;
                            }
                            ESP_LOGD(tag, "Set config STR %s = %s", argname, argv);
                            break;
                    }
                    if(isArgValueParsed) 
                    {
                        ++nParsed;
                    }
                }
                else
                {
                    nvs_handle_t hNVS;
                    esp_err_t ret;
                    ret = nvs_open("storage", NVS_READWRITE, &hNVS);
                    ESP_ERROR_CHECK(ret);
                    if(strcmp(argname, "WiFi_SSID"))
                    {
                        nvs_set_str(hNVS, "WiFi_SSID", argv);
                        ++nParsed;
                    }
                    else if(strcmp(argname, "WiFi_password"))
                    {
                        nvs_set_str(hNVS, "WiFi_password", argv);
                        ++nParsed;
                    }
                    nvs_close(hNVS);
                }
                // done parsing one argument, move to the next
                i = k + 1; j = i; k = i;
            }
            // if the position of = and & does not obey the rule, parsing terminates
            else
                break;
        }
        // sync values in config object with rotsensor object
        if(nParsed > 0)
        {
            push_config_to_volatile_variables(&cfg);
        }
        httpdStartResponse(connData, 200);
        httpdHeader(connData, "Content-Type", "text/plain");
        httpdEndHeaders(connData);
        sprintf(bufR, "%d", nParsed);
        httpdSend(connData, bufR, -1);
        ESP_LOGD(tag, "Set %d config items", nParsed);
    }
    return HTTPD_CGI_DONE;
}

static CgiStatus ICACHE_FLASH_ATTR onSave(HttpdConnData *connData)
{
    if (connData->isConnectionClosed) {
        return HTTPD_CGI_DONE;
    }
    /* Response BEGIN */
    httpdStartResponse(connData, 200);
    httpdHeader(connData, "Content-Type", "text/plain");
    httpdEndHeaders(connData);
    httpdSend(connData, "saving", -1);
    /* Response END */
    // TODO: send reset semaphore
    write_to_EEPROM_async();
    return HTTPD_CGI_DONE;
}


/*
* @args relayno=1&state=1&timems=0
* @arg relayno starts from 1
* @arg state 0:off 1:on
* @arg timems 0:forever
*/
static CgiStatus ICACHE_FLASH_ATTR onSetRelay(HttpdConnData *connData){
    char sVal[16];
    size_t lenVal;
    int relayno = -1,state = -1,timems = -1;
    bool isErr = true;
    char errmsg[256] = {0};
    if(connData->requestType == HTTPD_METHOD_POST)
    {
        lenVal = httpdFindArg(connData->post.buff, "relayno", sVal, sizeof(sVal));
        if(lenVal > 0)
            relayno = atoi(sVal); 
        else
            strcat(errmsg, "missing arg: relayno\n");
        lenVal = httpdFindArg(connData->post.buff, "state", sVal, sizeof(sVal));
        if(lenVal > 0)
            state = atoi(sVal);   
        else
            strcat(errmsg, "missing arg: state\n");
        lenVal = httpdFindArg(connData->post.buff, "timems", sVal, sizeof(sVal));
        if(lenVal > 0)
            timems = atoi(sVal);
        if(relayno > 0 && state >= 0)
        {
            isErr = false;
            strcpy(errmsg, "done");
            if(timems > 0)
            {
                set_timed_relay_state(relayno, state, timems);
            }
            else
            {
                set_relay_state(relayno, state);
            }
        }
        else
        {
            sprintf(errmsg, "invalid argument values relayno=%d, state=%d\n", relayno, state);
        }


    }
    if(!isErr)
        httpdStartResponse(connData, 200);
    else
        httpdStartResponse(connData, 300);
    httpdHeader(connData, "Content-Type", "text/plain");
    httpdEndHeaders(connData);
    httpdSend(connData, errmsg, -1);
    return HTTPD_CGI_DONE;
}


static CgiStatus ICACHE_FLASH_ATTR onGetRelay(HttpdConnData *connData)
{

    int relayno = -1,state = -1,timems = -1;
    char msg[256] = {0};

    for(int i = 0; i < N_RELAYS; ++i)
    {
        char msgsub[128] = {0};
        Relay_t* p = &Relays[i];
        bool state = relay_get_state(p);
        uint64_t timems_remain = RelaysTimemsRemain[i];
        sprintf(msgsub, "relayno=%d&state=%d&timems_remain=%d\n", i+1, state, timems_remain);
        strcat(msg, msgsub);
    }

    httpdStartResponse(connData, 200);
    httpdHeader(connData, "Content-Type", "text/plain");
    httpdEndHeaders(connData);
    httpdSend(connData, msg, -1);
    return HTTPD_CGI_DONE;

}




typedef struct {
        camera_fb_t* fb;
        size_t idxNext;
} httpd_chunking_t;


static CgiStatus ICACHE_FLASH_ATTR onCamCapture(HttpdConnData *connData)
{
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t fb_len = 0;
    bool isErr = false;
    size_t maxlenPayload = HTTPD_MAX_SENDBUFF_LEN - HTTPD_MAX_HEAD_LEN;
    if(connData->cgiData == NULL) // sending first response
    {
        fb = get_camera_fb();
        if (!fb) 
        {
            ESP_LOGE(tag, "Camera capture failed");
            isErr = true;
            goto LABEL_CAM_CAPTURE_FAILED;
        }
        /* send header BEGIN */
        httpdStartResponse(connData, 200);
        httpdHeader(connData, "Content-Type", "image/jpeg");
        httpdHeader(connData, "Content-Disposition", "inline; filename=capture.jpg");
        httpdEndHeaders(connData);
        /* send header END */

        httpd_chunking_t* pChk = malloc(sizeof(httpd_chunking_t));
        pChk->fb = fb;
        pChk->idxNext = 0;
        connData->cgiData = (void*)pChk;
        if(fb->len > maxlenPayload ) // picture too big for data, need to split
        {
            httpdSend(connData, fb->buf, maxlenPayload);
            pChk->idxNext += maxlenPayload;
            return HTTPD_CGI_MORE;
        }
        else // picture can fit in buffer, send as whole
        {
            httpdSend(connData, fb->buf, fb->len);

            return HTTPD_CGI_DONE;
        }

    }
    else // sending MORE
    {
        httpd_chunking_t* pChk = (httpd_chunking_t*)connData->cgiData;
        size_t lenRemain = pChk->fb->len - pChk->idxNext;
        if(lenRemain <= maxlenPayload)
        {
            httpdSend(connData, pChk->fb->buf + pChk->idxNext, lenRemain);
            return_camera_fb(pChk->fb);
            if(pChk) free(pChk);
            return HTTPD_CGI_DONE;
        }
        else
        {
            httpdSend(connData, pChk->fb->buf + pChk->idxNext, maxlenPayload);
            pChk->idxNext += maxlenPayload;
            return HTTPD_CGI_MORE;
        }
        
    }

LABEL_CAM_CAPTURE_FAILED:
    httpdStartResponse(connData, 300);
    httpdHeader(connData, "Content-Type", "plain/text");
    httpdEndHeaders(connData);
    httpdSend(connData, "failed to capture picture", -1);
    return HTTPD_CGI_DONE;
}


/* Websocket handlers END */

/*
This is the main url->function dispatching data struct.
In short, it's a struct with various URLs plus their handlers. The handlers can
be 'standard' CGI functions you wrote, or 'special' CGIs requiring an argument.
They can also be auth-functions. An asterisk will match any url starting with
everything before the asterisks; "*" matches everything. The list will be
handled top-down, so make sure to put more specific rules above the more
general ones. Authorization things (like authBasic) act as a 'barrier' and
should be placed above the URLs they protect.
*/
#define OTA_FLASH_SIZE_K 1024
#define OTA_TAGNAME "generic"

CgiUploadFlashDef uploadParams={
	.type=CGIFLASH_TYPE_FW,
	.fw1Pos=0x1000,
	.fw2Pos=((OTA_FLASH_SIZE_K*1024)/2)+0x1000,
	.fwSize=((OTA_FLASH_SIZE_K*1024)/2)-0x1000,
	.tagName=OTA_TAGNAME
};


const HttpdBuiltInUrl builtInUrls[] = {
    // ROUTE_CGI_ARG("*", cgiRedirectApClientToHostname, "esp8266.nonet"),
    //Routines to make the /wifi URL and everything beneath it work.
    //Enable the line below to protect the WiFi configuration with an username/password combo.
    //	{"/wifi/*", authBasic, myPassFn},

    // ROUTE_REDIRECT("/wifi", "/wifi/wifi.tpl"),
    // ROUTE_REDIRECT("/wifi/", "/wifi/wifi.tpl"),
    // ROUTE_CGI("/wifi/wifiscan.cgi", cgiWiFiScan),
    // ROUTE_TPL("/wifi/wifi.tpl", tplWlan),
    // ROUTE_CGI("/wifi/connect.cgi", cgiWiFiConnect),
    // ROUTE_CGI("/wifi/connstatus.cgi", cgiWiFiConnStatus),
    // ROUTE_CGI("/wifi/setmode.cgi", cgiWiFiSetMode),
    // ROUTE_CGI("/wifi/startwps.cgi", cgiWiFiStartWps),
    // ROUTE_CGI("/wifi/ap", cgiWiFiAPSettings),

    // ROUTE_WS("/websocket/ws.cgi", myWebsocketConnect),


    // Files in /static dir are assumed to never change, so send headers to encourage browser to cache forever.
    // ROUTE_FILE_EX("/static/*", &CgiOptionsEspfsStatic),
    /* homepage BEGIN */
    ROUTE_REDIRECT("/", "/index.html"),
    ROUTE_REDIRECT("/index", "/index.html"),
    ROUTE_REDIRECT("/config", "/config.html"),
    /* homepage END */
    /* Functions BEGIN */
    ROUTE_CGI("/reset", onReset),
    ROUTE_CGI("/save", onSave),
    ROUTE_CGI("/getrelay", onGetRelay),
    ROUTE_CGI("/setrelay", onSetRelay),
    ROUTE_CGI("/capture", onCamCapture),
    ROUTE_CGI("/setwifi", onSetWiFi),
    /* Functions END */

    /* websocket */
    // ROUTE_WS("/ws", onWebsocketConnect),
    /* OTA BEGIN */
    ROUTE_REDIRECT("/flash", "/flash/index.html"),
	ROUTE_REDIRECT("/flash/", "/flash/index.html"),
	ROUTE_CGI("/flash/flashinfo.json", cgiGetFlashInfo),
	ROUTE_CGI("/flash/setboot", cgiSetBoot),
	ROUTE_CGI_ARG("/flash/upload", cgiUploadFirmware, &uploadParams),
	ROUTE_CGI_ARG("/flash/erase", cgiEraseFlash, &uploadParams),
	ROUTE_CGI("/flash/reboot", cgiRebootFirmware),
    /* OTA END */

    ROUTE_FILESYSTEM(),

    ROUTE_END()
};




void init_webserver()
{
    /* init OS handles */
    mtxWebBuf1 = xSemaphoreCreateMutex();
    mtxWSSending = xSemaphoreCreateMutex();
    /* init FileSystem BEGIN */
    
    // TODO: how to define espfs_image_bin
    EspFsConfig espfs_conf = {
        .memAddr = espfs_image_bin,
    };
    EspFs* fs = espFsInit(&espfs_conf);
    httpdRegisterEspfs(fs);
    /* init FileSystem END */

    esp_netif_init();

    // TCP/IP
    httpdFreertosInit(&httpdFreertosInstance,
                    builtInUrls,
                    LISTEN_PORT,
                    connectionMemory,
                    MAX_CONNECTIONS,
                    HTTPD_FLAG_NONE);
    httpdFreertosStart(&httpdFreertosInstance);
}