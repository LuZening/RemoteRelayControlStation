#pragma once

#include "my_network.h"
#include "espfs.h"
#include "espfs_image.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// Web
#define LISTEN_PORT     80u
#define MAX_CONNECTIONS 4u
// TODO: determine FS Addr
#define WEBSERVER_FS_ADDR_BEGIN (const char*)(0x00)

// only 1 Websocket sending request is allowed each time
extern SemaphoreHandle_t mtxWSSending;

void init_webserver();
// int websocket_broadcast(const char* wsURL, const char* data, int len);