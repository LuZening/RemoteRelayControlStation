#pragma once

#include "esp_camera.h"
#ifdef FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif
#define CAM_USE_MUTEX


#ifdef CAM_USE_MUTEX
extern SemaphoreHandle_t mtxCam;
#endif
esp_err_t setup_camera();

camera_fb_t* get_camera_fb();

void return_camera_fb(camera_fb_t* p);