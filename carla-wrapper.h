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

// debug
#define TRACE_CALL printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %s %d\n", __FUNCTION__, __LINE__);

struct carla_priv;

// --------------------------------------------------------------------------------------------------------------------
// carla + obs integration methods

struct carla_priv *carla_priv_create(obs_source_t *source, uint32_t bufferSize, uint32_t sampleRate);
void carla_priv_destroy(struct carla_priv *carla);

void carla_priv_activate(struct carla_priv *carla);
void carla_priv_deactivate(struct carla_priv *carla);
void carla_priv_process_audio(struct carla_priv *carla, float *buffers[2], uint32_t frames);

void carla_priv_idle(struct carla_priv *carla);

char *carla_priv_get_state(struct carla_priv *carla);
void carla_priv_set_state(struct carla_priv *carla, const char *state);

void carla_priv_readd_properties(struct carla_priv *carla, obs_properties_t *props);

bool carla_priv_select_plugin_callback(obs_properties_t *props, obs_property_t *property, void *data);
bool carla_priv_show_gui_callback(obs_properties_t *props, obs_property_t *property, void *data);

void carla_priv_free(void *data);

// --------------------------------------------------------------------------------------------------------------------
