/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <obs-module.h>

// property names
#define PROP_LOAD_FILE "load-file"
#define PROP_SELECT_PLUGIN "select-plugin"
#define PROP_SHOW_GUI "show-gui"

// maximum buffer used, can be smaller
#define MAX_AUDIO_BUFFER_SIZE 512

#define CARLA_MAX_PARAMS 100

#define PARAM_NAME_SIZE 5
#define PARAM_NAME_INIT {'p','0','0','0','\0'}

enum buffer_size_mode {
    buffer_size_dynamic,
    buffer_size_static_128,
    buffer_size_static_256,
    buffer_size_static_512,
    buffer_size_static_max = buffer_size_static_512
};

struct carla_priv;

// --------------------------------------------------------------------------------------------------------------------
// helper methods

static inline
uint32_t bufsize_mode_to_frames(enum buffer_size_mode bufsize)
{
    switch (bufsize)
    {
    case buffer_size_dynamic:
        return MAX_AUDIO_BUFFER_SIZE;
    case buffer_size_static_128:
        return 128;
    case buffer_size_static_256:
        return 256;
    case buffer_size_static_512:
        return 512;
    }
    return 0;
}

static inline
void param_index_to_name(uint32_t index, char name[PARAM_NAME_SIZE])
{
    name[1] = '0' + ((index / 100) % 10);
    name[2] = '0' + ((index / 10 ) % 10);
    name[3] = '0' + ((index / 1  ) % 10);
}

static inline
void remove_all_props(obs_properties_t *props, obs_data_t *settings)
{
    obs_data_unset_default_value(settings, PROP_SHOW_GUI);
    obs_properties_remove_by_name(props, PROP_SHOW_GUI);

    char pname[PARAM_NAME_SIZE] = PARAM_NAME_INIT;

    for (uint32_t i=0; i < CARLA_MAX_PARAMS; ++i)
    {
        param_index_to_name(i, pname);
        obs_data_unset_default_value(settings, pname);
        obs_properties_remove_by_name(props, pname);
    }
}

// --------------------------------------------------------------------------------------------------------------------
// carla + obs integration methods

#ifdef __cplusplus
extern "C" {
#endif

struct carla_priv *carla_priv_create(obs_source_t *source, enum buffer_size_mode bufsize, uint32_t srate, bool filter);
void carla_priv_destroy(struct carla_priv *carla);

void carla_priv_activate(struct carla_priv *carla);
void carla_priv_deactivate(struct carla_priv *carla);
void carla_priv_process_audio(struct carla_priv *carla, float *buffers[2], uint32_t frames);

void carla_priv_idle(struct carla_priv *carla);

char *carla_priv_get_state(struct carla_priv *carla);
void carla_priv_set_state(struct carla_priv *carla, const char *state);
void carla_priv_set_buffer_size(struct carla_priv *priv, enum buffer_size_mode bufsize);

void carla_priv_readd_properties(struct carla_priv *carla, obs_properties_t *props, bool reset);

bool carla_priv_load_file_callback(obs_properties_t *props, obs_property_t *property, void *data);
bool carla_priv_select_plugin_callback(obs_properties_t *props, obs_property_t *property, void *data);
bool carla_priv_show_gui_callback(obs_properties_t *props, obs_property_t *property, void *data);

void carla_priv_free(void *data);

#ifdef __cplusplus
}
#endif

// --------------------------------------------------------------------------------------------------------------------
