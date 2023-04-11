/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void carla_obs_callback_on_main_thread(void (*callback)(void *param), void *param);

uintptr_t carla_obs_get_main_window_id(void);
void* carla_obs_get_main_window_qt_widget_ptr(void);

#ifdef __cplusplus
}
#endif
