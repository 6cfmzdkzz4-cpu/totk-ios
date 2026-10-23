/* Private development relay; only actual completed, composed renderer pixels. */
#pragma once
#include "totk_live_display.h"
#include <stddef.h>
void totk_duo_capture_pixels(const void *pixels,size_t width,size_t height,size_t row,int bgra,TotkLiveSource source);
