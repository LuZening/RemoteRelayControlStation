#pragma once

#include "esp_http_client.h"

bool http_post_cam_picture(const char* camid, const char* data, size_t len);