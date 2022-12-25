idf_component_register(SRCS "webserver.c"  "relay.c" "main.c" "my_network.c" 
                    INCLUDE_DIRS "."
                    REQUIRES "driver" "esp_adc_cal" "libesphttpd" "ButtonDebouncer" "esp_wifi" "nvs_flash" "fatfs" "vfs" "tcpip_adapter"
                       "esp_timer" "esp32-camera")