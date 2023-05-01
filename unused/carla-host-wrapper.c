/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-wrapper.h"
#include "common.h"
#include "qtutils.h"
#include <util/platform.h>

// IDE helpers, must match cmake config
#define CARLA_PLUGIN_BUILD 1
#define HAVE_X11 1
#define REAL_BUILD 1
#define STATIC_PLUGIN_TARGET 1

#include "CarlaHostPlugin.h"
#include "CarlaFrontend.h"

// generates a warning if this is defined as anything else
#define CARLA_API

// ----------------------------------------------------------------------------
// private data methods

struct carla_param_data {
	uint32_t hints;
	float min, max;
};

struct carla_priv {
	obs_source_t *source;
	uint32_t bufferSize;
	double sampleRate;
	CarlaHostHandle handle;

	// cached parameter info
	uint32_t paramCount;
	struct carla_param_data *paramDetails;

	// update properties when timeout is reached, 0 means do nothing
	uint64_t update_request;

	// keep track of active state
	volatile bool activated;
};

// ----------------------------------------------------------------------------
// carla + obs integration methods

struct carla_priv *carla_priv_create(obs_source_t *source,
				     enum buffer_size_mode bufsize,
				     uint32_t srate)
{
	struct carla_priv *priv = bzalloc(sizeof(struct carla_priv));
	if (priv == NULL)
		return NULL;

	priv->source = source;
	priv->bufferSize = bufsize_mode_to_frames(bufsize);
	priv->sampleRate = srate;

	assert(priv->bufferSize != 0);
	if (priv->bufferSize == 0)
		goto fail1;

	priv->handle = carla_create_host_plugin_handle();
	if (priv->handle == NULL)
		goto fail1;

	// TODO build and setup local bridges
	carla_set_engine_option(priv->handle,
				ENGINE_OPTION_PATH_BINARIES, 0,
				"/usr/lib/carla");
	carla_set_engine_option(priv->handle,
				ENGINE_OPTION_PATH_RESOURCES, 0,
				"/usr/share/carla/resources");
	carla_set_engine_option(priv->handle,
				ENGINE_OPTION_PREFER_PLUGIN_BRIDGES, 1, NULL);

	// TODO set sample rate etc

	return priv;

fail1:
	bfree(priv);
	return NULL;
}

void carla_priv_destroy(struct carla_priv *priv)
{
	if (priv->activated)
		carla_priv_deactivate(priv);

	carla_host_handle_free(priv->handle);
	bfree(priv->paramDetails);
	bfree(priv);
}

// ----------------------------------------------------------------------------

void carla_priv_activate(struct carla_priv *priv)
{
	assert(!priv->activated);
	carla_set_active(priv->handle, 0, true);
	priv->activated = true;
}

void carla_priv_deactivate(struct carla_priv *priv)
{
	assert(priv->activated);
	priv->activated = false;
	carla_set_active(priv->handle, 0, false);
}

void carla_priv_process_audio(struct carla_priv *priv, float *buffers[MAX_AV_PLANES],
			      uint32_t frames)
{
	// TODO process
// 	priv->timeInfo.usecs = os_gettime_ns() / 1000;
// 	priv->descriptor->process(priv->handle, buffers, buffers, frames, NULL,
// 				  0);
}

void carla_priv_idle(struct carla_priv *priv)
{
	carla_engine_idle(priv->handle);
	handle_update_request(priv->source, &priv->update_request);
}

char *carla_priv_get_state(struct carla_priv *priv)
{
	return carla_host_save_state(priv->handle);
}

void carla_priv_set_state(struct carla_priv *priv, const char *state)
{
	carla_host_load_state(priv->handle, state);
}

// ----------------------------------------------------------------------------

void carla_priv_set_buffer_size(struct carla_priv *priv,
				enum buffer_size_mode bufsize)
{
	const uint32_t new_buffer_size = bufsize_mode_to_frames(bufsize);
	assert(new_buffer_size != 0);
	if (new_buffer_size == 0)
		return;

	const bool activated = priv->activated;

	if (activated)
		carla_priv_deactivate(priv);

	priv->bufferSize = new_buffer_size;
	// TODO set buffer size
// 	priv->descriptor->dispatcher(priv->handle,
// 				     NATIVE_PLUGIN_OPCODE_BUFFER_SIZE_CHANGED,
// 				     new_buffer_size, 0, NULL, 0.f);

	if (activated)
		carla_priv_activate(priv);
}

// ----------------------------------------------------------------------------

static bool carla_priv_param_changed(void *data, obs_properties_t *props,
				     obs_property_t *property,
				     obs_data_t *settings)
{
	UNUSED_PARAMETER(props);

	struct carla_priv *priv = data;

	const char *const pname = obs_property_name(property);
	if (pname == NULL)
		return false;

	const char *pname2 = pname + 1;
	while (*pname2 == '0')
		++pname2;

	const int pindex = atoi(pname2);

	if (pindex < 0 || pindex >= (int)priv->paramCount)
		return false;

	const float min = priv->paramDetails[pindex].min;
	const float max = priv->paramDetails[pindex].max;

	float value;
	switch (obs_property_get_type(property)) {
	case OBS_PROPERTY_BOOL:
		value = obs_data_get_bool(settings, pname) ? max : min;
		break;
	case OBS_PROPERTY_INT:
		value = obs_data_get_int(settings, pname);
		if (value < min)
			value = min;
		else if (value > max)
			value = max;
		break;
	case OBS_PROPERTY_FLOAT:
		value = obs_data_get_double(settings, pname);
		if (value < min)
			value = min;
		else if (value > max)
			value = max;
		break;
	default:
		return false;
	}

	// printf("param changed %d:%s %f\n", pindex, pname, value);
	carla_set_parameter_value(priv->handle, 0, pindex, value);

	return false;
}

void carla_priv_readd_properties(struct carla_priv *priv,
				 obs_properties_t *props, bool reset)
{
	if (carla_get_current_plugin_count(priv->handle) == 0)
		return;

	obs_data_t *settings = obs_source_get_settings(priv->source);

	// show/hide GUI button
	if (carla_get_plugin_info(priv->handle, 0)->hints & PLUGIN_HAS_CUSTOM_UI) {
		obs_properties_add_button2(props, PROP_SHOW_GUI,
					   obs_module_text("Show custom GUI"),
					   carla_priv_show_gui_callback, priv);
	}

	uint32_t params = carla_get_parameter_count(priv->handle, 0);
	if (params > MAX_PARAMS)
		params = MAX_PARAMS;

	bfree(priv->paramDetails);
	priv->paramCount = params;
	priv->paramDetails = bzalloc(sizeof(struct carla_param_data) * params);

	char pname[PARAM_NAME_SIZE] = PARAM_NAME_INIT;

	for (uint32_t i = 0; i < params; ++i) {
		const ParameterData *const data =
			carla_get_parameter_data(priv->handle, 0, i);

		if (data->type != PARAMETER_INPUT)
			continue;
		if ((data->hints & PARAMETER_IS_ENABLED) == 0)
			continue;

		const CarlaParameterInfo *const info =
			carla_get_parameter_info(priv->handle, 0, i);

		const ParameterRanges *const ranges =
			carla_get_parameter_ranges(priv->handle, 0, i);

		param_index_to_name(i, pname);
		priv->paramDetails[i].hints = data->hints;
		priv->paramDetails[i].min = ranges->min;
		priv->paramDetails[i].max = ranges->max;

		obs_property_t *prop;

		if (data->hints & PARAMETER_IS_BOOLEAN) {
			prop = obs_properties_add_bool(props, pname,
						       info->name);

			obs_data_set_default_bool(settings, pname,
						  ranges->def ==
							  ranges->max);

			if (reset)
				obs_data_set_bool(settings, pname,
						  ranges->def ==
							  ranges->max);
		} else if (data->hints & PARAMETER_IS_INTEGER) {
			prop = obs_properties_add_int_slider(
				props, pname, info->name, ranges->min,
				ranges->max, ranges->step);

			obs_data_set_default_int(settings, pname,
						 ranges->def);

			if (info->unit && *info->unit)
				obs_property_int_set_suffix(prop, info->unit);

			if (reset)
				obs_data_set_int(settings, pname,
						 ranges->def);
		} else {
			prop = obs_properties_add_float_slider(
				props, pname, info->name, ranges->min,
				ranges->max, ranges->step);

			obs_data_set_default_double(settings, pname,
						    ranges->def);

			if (info->unit && *info->unit)
				obs_property_float_set_suffix(prop, info->unit);

			if (reset)
				obs_data_set_double(settings, pname,
						    ranges->def);
		}

		obs_property_set_modified_callback2(
			prop, carla_priv_param_changed, priv);
	}

	obs_data_release(settings);
}

// ----------------------------------------------------------------------------

static bool carla_post_load_callback(struct carla_priv *priv,
				     obs_properties_t *props)
{
	obs_source_t *source = priv->source;
	obs_data_t *settings = obs_source_get_settings(source);
	remove_all_props(props, settings);
	carla_priv_readd_properties(priv, props, true);
	obs_data_release(settings);
	return true;
}

bool carla_priv_load_file_callback(obs_properties_t *props,
				   obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(property);

	struct carla_priv *priv = data;

	const char *filename = carla_qt_file_dialog(
		false, false, obs_module_text("Load File"), NULL);

	if (filename == NULL)
		return false;

	if (carla_get_current_plugin_count(priv->handle) != 0)
		carla_replace_plugin(priv->handle, 0);

	if (carla_load_file(priv->handle, filename))
		return carla_post_load_callback(priv, props);

	return false;
}

bool carla_priv_select_plugin_callback(obs_properties_t *props,
				       obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(property);

	struct carla_priv *priv = data;

	const PluginListDialogResults *plugin =
		carla_frontend_createAndExecPluginListDialog(
			carla_qt_get_main_window());

	if (plugin == NULL)
		return false;

	if (carla_get_current_plugin_count(priv->handle) != 0)
		carla_replace_plugin(priv->handle, 0);

	if (carla_add_plugin(priv->handle, plugin->build,
			     plugin->type, plugin->filename, plugin->name,
			     plugin->label, plugin->uniqueId, NULL,
			     PLUGIN_OPTIONS_NULL))
		return carla_post_load_callback(priv, props);

	return false;
}

bool carla_priv_show_gui_callback(obs_properties_t *props,
				  obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	struct carla_priv *priv = data;

	char winIdStr[24];
	snprintf(winIdStr, sizeof(winIdStr), "%llx",
		 (ulonglong)carla_qt_get_main_window_id());
	carla_set_engine_option(priv->handle,
				ENGINE_OPTION_FRONTEND_WIN_ID, 0, winIdStr);

	const double scaleFactor = carla_qt_get_scale_factor();
	carla_set_engine_option(priv->handle,
				ENGINE_OPTION_FRONTEND_UI_SCALE,
				scaleFactor * 1000, NULL);

	carla_show_custom_ui(priv->handle, 0, true);

	return false;
}

// ----------------------------------------------------------------------------

void carla_priv_free(void *data)
{
	free(data);
}

// ----------------------------------------------------------------------------
