/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
#include <QtWidgets/QMainWindow>
extern "C" {
#else
typedef struct QMainWindow QMainWindow;
#endif

void carla_qt_callback_on_main_thread(void (*callback)(void *param), void *param);

const char* carla_qt_file_dialog(bool save, bool isDir, const char *title, const char *filter);

uintptr_t carla_qt_get_main_window_id(void);
QMainWindow* carla_qt_get_main_window(void);
double carla_qt_get_scale_factor(void);

#ifdef __cplusplus
}
#endif
