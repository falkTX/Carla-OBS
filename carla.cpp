/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <obs-module.h>

bool obs_module_load(void)
{
    static const struct obs_source_info carla_obs_plugin_info = {};
    obs_register_source(&carla_obs_plugin_info);
    return true;
}
