#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "webclient.h"
#include "relay.h"
#include "config.h"

#include "esp_http_client.h"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 4096
static const char *tag = "HTTP_CLIENT";


static esp_err_t _http_event_handler(esp_http_client_event_t *evt);



bool fetch_cmd_from_server(const char* URL, int method, const char* argsReq,  char* argsResp, size_t* plenArgsResp)
{
    bool isOK = false;
    
}

bool http_post_cam_picture(const char* device_name, const char* data, size_t len)
{
    static char query[128];
    bool isOK = true;
    if(!cfg.enable_upload_to_server)
    {
        ESP_LOGE("upload to server not enabled");
        goto failed;
    }
    sprintf(query, "device=%s", device_name);
    esp_http_client_config_t config = {
        .url = cfg.upload_dest_URL,
        .path = "/postpicture",
        .query = device_name,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .event_handler = _http_event_handler,
    };
    esp_err_t err;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    err = esp_http_client_set_header(client, "Content-Type", "image/jpeg");
    if(err != ESP_OK)
        goto failed;
    // esp_http_client_set_header(client, "Content-Disposition", "inline; filename=capture.jpg");
    err = esp_http_client_set_post_field(client, data, len );
    if(err != ESP_OK)
        goto failed;
    err = esp_http_client_perform(client);
    if(err != ESP_OK)
        goto failed;
    ESP_LOGI(tag, "Image %uB uploaded to %s", len, cfg.upload_dest_URL);        
    return true;
failed:
    ESP_LOGE(tag, "Failed uploading image %uB to %s", len, cfg.upload_dest_URL);       
    return false; 
}



static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(tag, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(tag, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(tag, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(tag, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(tag, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(tag, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(tag, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(tag, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(tag, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(tag, "Last esp error code: 0x%x", err);
                ESP_LOGI(tag, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
    }
    return ESP_OK;
}
