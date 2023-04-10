/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <obs-module.h>

static const char *carla_obs_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("3BandEq");
}

static void *carla_obs_create(obs_data_t *settings, obs_source_t *filter)
{
	return nullptr;
}

static void carla_obs_destroy(void *data)
{
}

static void carla_obs_update(void *data, obs_data_t *settings)
{
}

static struct obs_audio_data *carla_obs_filter_audio(void *data, struct obs_audio_data *audio)
{
    return audio;
}

static void carla_obs_get_defaults(obs_data_t *defaults)
{
}

static obs_properties_t *carla_obs_get_properties(void *unused)
{
    obs_properties_t *props = obs_properties_create();
    return props;
}

static void carla_obs_save(void *data, obs_data_t *settings)
{
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("carla", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
    return "Carla Plugin Host";
}

bool obs_module_load(void)
{
    static struct obs_source_info filter = {};
    filter.id = "carla_filter";
    filter.type = OBS_SOURCE_TYPE_FILTER;
    filter.output_flags = OBS_SOURCE_AUDIO;
    filter.get_name = carla_obs_get_name;
    filter.create = carla_obs_create;
    filter.destroy = carla_obs_destroy;
    filter.update = carla_obs_update;
    filter.filter_audio = carla_obs_filter_audio;
    filter.get_defaults = carla_obs_get_defaults,
    filter.get_properties = carla_obs_get_properties;
    filter.save = carla_obs_save;

    obs_register_source(&filter);
    return true;
}
