/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <obs-module.h>

// property names
#define PROP_SELECT_PLUGIN "select-plugin"
#define PROP_SHOW_GUI "show-gui"
#define PROP_RELOAD "reload"

// maximum buffer used, could be lower
#define MAX_AUDIO_BUFFER_SIZE 512

// debug
#define TRACE_CALL printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %s %d\n", __FUNCTION__, __LINE__);

// --------------------------------------------------------------------------------------------------------------------
// carla + obs integration methods

struct carla_priv;

struct carla_priv *carla_obs_alloc(obs_source_t *source, uint32_t bufferSize, uint32_t sampleRate);
void carla_obs_dealloc(struct carla_priv *carla);

void carla_obs_activate(struct carla_priv *carla);
void carla_obs_deactivate(struct carla_priv *carla);
void carla_obs_process_audio(struct carla_priv *carla, float *buffers[2], uint32_t frames);

void carla_obs_idle(struct carla_priv *carla);

char *carla_obs_get_state(struct carla_priv *carla);
void carla_obs_set_state(struct carla_priv *carla, const char *state);

void carla_obs_readd_properties(struct carla_priv *carla, obs_properties_t *props);

bool carla_obs_select_plugin_callback(obs_properties_t *props, obs_property_t *property, void *data);
bool carla_obs_show_gui_callback(obs_properties_t *props, obs_property_t *property, void *data);

void carla_obs_free(void *data);

// --------------------------------------------------------------------------------------------------------------------
