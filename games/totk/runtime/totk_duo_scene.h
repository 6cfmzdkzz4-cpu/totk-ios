/* Private actual-UIKit scene/input reader; inactive without DM_DUO_RELAY. */
#pragma once
#include "runtime.h"
int totk_duo_scene_configured(void);
int totk_duo_scene_active(void);
void totk_duo_scene_read(RuntimeGamepadState *state);
