/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>

#ifdef __cplusplus
#include <QtWidgets/QMainWindow>
extern "C" {
#else
typedef struct QMainWindow QMainWindow;
#endif

void carla_obs_callback_on_main_thread(void (*callback)(void *param), void *param);

uintptr_t carla_obs_get_main_window_id(void);
QMainWindow* carla_obs_get_main_window(void);

#ifdef __cplusplus
}
#endif
