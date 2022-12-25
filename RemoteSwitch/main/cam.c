
#include "cam.h"
#define tag "cam"

static camera_config_t camera_config = {
    .pin_pwdn = 11,
    .pin_reset = 9,
    .pin_xclk = 14,
    .pin_sccb_sda = 20,
    .pin_sccb_scl = 19,

    .pin_d7 = 13,
    .pin_d6 = 21,
    .pin_d5 = 47,
    .pin_d4 = 45,
    .pin_d3 = 39,
    .pin_d2 = 42,
    .pin_d1 = 41,
    .pin_d0 = 38,
    .pin_vsync = 10,
    .pin_href = 12,
    .pin_pclk = 48,

    //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 16000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, //YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_SVGA,    //QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

    .jpeg_quality = 12, //0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 1,       //When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};



esp_err_t setup_camera()
{
    #ifdef FREERTOS
    mtxCam = xSemaphoreCreateMutex();
    #endif
    //initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(tag, "Camera Init Failed");
        return err;
    }

    return ESP_OK;
}


camera_fb_t* get_camera_fb()
{
#ifdef CAM_USE_MUTEX
    xSemaphoreTake(mtxCam, portMAX_DELAY);
#endif
    camera_fb_t* fb;
    fb = esp_camera_fb_get();
    if(fb)
        return fb;
    else
    {
        #ifdef CAM_USE_MUTEX
            xSemaphoreGive(mtxCam);
        #endif
        ESP_LOGE(tag, "failed capturing camera");
        return NULL;
    }
}

void return_camera_fb(camera_fb_t* fb)
{
    if(fb)
    {
        esp_camera_fb_return(fb);
        #ifdef CAM_USE_MUTEX
            xSemaphoreGive(mtxCam);
        #endif
    }
    else
        return;

}