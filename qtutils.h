/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct carla_obs_idle_callback_data;
typedef struct carla_obs_idle_callback_data carla_obs_idle_callback_data_t;

typedef void (*carla_obs_idle_callback_t)(void *data);

carla_obs_idle_callback_data_t* carla_obs_add_idle_callback(carla_obs_idle_callback_t cb, void *data);
void carla_obs_remove_idle_callback(carla_obs_idle_callback_data_t *cbdata);

uintptr_t carla_obs_get_main_window_id(void);

#ifdef __cplusplus
}
#endif
