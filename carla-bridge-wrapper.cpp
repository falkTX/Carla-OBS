/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-bridge.hpp"
#include "carla-wrapper.h"
#include "qtutils.h"
#include <util/platform.h>

#include "CarlaFrontend.h"

// generates a warning if this is defined as anything else
#define CARLA_API

// --------------------------------------------------------------------------------------------------------------------
// private data methods

struct carla_priv : carla_bridge_callback {
	obs_source_t *source = nullptr;
	uint32_t bufferSize = 0;
	double sampleRate = 0;

	// update properties when timeout is reached, 0 means do nothing
	uint64_t update_requested = 0;

	carla_bridge bridge;

	void bridge_parameter_changed(uint index, float value) override
	{
		char pname[PARAM_NAME_SIZE] = PARAM_NAME_INIT;
		param_index_to_name(index, pname);

		// obs_source_t *source = priv->source;
		obs_data_t *settings = obs_source_get_settings(source);

		/**/ if (bridge.paramDetails[index].hints &
			 PARAMETER_IS_BOOLEAN)
			obs_data_set_bool(settings, pname,
					  value > 0.5f ? 1.f : 0.f);
		else if (bridge.paramDetails[index].hints &
			 PARAMETER_IS_INTEGER)
			obs_data_set_int(settings, pname, value);
		else
			obs_data_set_double(settings, pname, value);

		obs_data_release(settings);

		update_requested = os_gettime_ns();
	}
};

// --------------------------------------------------------------------------------------------------------------------
// carla + obs integration methods

struct carla_priv *carla_priv_create(obs_source_t *source,
				     enum buffer_size_mode bufsize,
				     uint32_t srate)
{
	struct carla_priv *priv = new struct carla_priv;
	if (priv == NULL)
		return NULL;

	priv->bridge.callback = priv;
	priv->source = source;
	priv->bufferSize = bufsize_mode_to_frames(bufsize);
	priv->sampleRate = srate;

	assert(priv->bufferSize != 0);
	if (priv->bufferSize == 0)
		goto fail1;

	return priv;

fail1:
	delete priv;
	return nullptr;
}

void carla_priv_destroy(struct carla_priv *priv)
{
	priv->bridge.cleanup();
	delete priv;
}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_activate(struct carla_priv *priv)
{
	priv->bridge.activate();
}

void carla_priv_deactivate(struct carla_priv *priv)
{
	priv->bridge.deactivate();
}

void carla_priv_process_audio(struct carla_priv *priv, float *buffers[2],
			      uint32_t frames)
{
	priv->bridge.process(buffers, frames);
}

void carla_priv_idle(struct carla_priv *priv)
{
	if (!priv->bridge.idle()) {
		// bridge crashed!
		// TODO something
	}

	if (priv->update_requested != 0) {
		const uint64_t now = os_gettime_ns();

		// request in the future?
		if (now < priv->update_requested) {
			priv->update_requested = now;
			return;
		}

		if (now - priv->update_requested >= 100000000ULL) // 100ms
		{
			priv->update_requested = 0;

			signal_handler_t *sighandler =
				obs_source_get_signal_handler(priv->source);
			signal_handler_signal(sighandler, "update_properties",
					      NULL);
		}
	}
}

char *carla_priv_get_state(struct carla_priv *priv)
{
	return NULL;
}

void carla_priv_set_state(struct carla_priv *priv, const char *state) {}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_set_buffer_size(struct carla_priv *priv,
				enum buffer_size_mode bufsize)
{
}

// --------------------------------------------------------------------------------------------------------------------

static bool carla_priv_param_changed(void *data, obs_properties_t *props,
				     obs_property_t *property,
				     obs_data_t *settings)
{
	UNUSED_PARAMETER(props);

	struct carla_priv *priv = static_cast<struct carla_priv *>(data);

	const char *const pname = obs_property_name(property);
	if (pname == NULL)
		return false;

	const char *pname2 = pname + 1;
	while (*pname2 == '0')
		++pname2;

	const int pindex = atoi(pname2);

	if (pindex < 0 || pindex >= (int)priv->bridge.paramCount)
		return false;

	const uint index = static_cast<uint>(pindex);

	const float min = priv->bridge.paramDetails[index].min;
	const float max = priv->bridge.paramDetails[index].max;

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

	priv->bridge.set_value(index, value);

	return false;
}

void carla_priv_readd_properties(struct carla_priv *priv,
				 obs_properties_t *props, bool reset)
{
	obs_data_t *settings = obs_source_get_settings(priv->source);

	if (priv->bridge.isRunning()) {
		obs_properties_add_button2(props, PROP_SHOW_GUI,
					   obs_module_text("Show custom GUI"),
					   carla_priv_show_gui_callback, priv);
	}

	char pname[PARAM_NAME_SIZE] = PARAM_NAME_INIT;

	for (uint32_t i = 0; i < priv->bridge.paramCount; ++i) {
		const carla_param_data &param(priv->bridge.paramDetails[i]);

		if ((param.hints & PARAMETER_IS_ENABLED) == 0)
			continue;

		obs_property_t *prop;
		param_index_to_name(i, pname);

		if (param.hints & PARAMETER_IS_BOOLEAN) {
			prop = obs_properties_add_bool(props, pname,
						       param.name);

			obs_data_set_default_bool(settings, pname,
						  carla_isEqual(param.def,
								param.max));

			if (reset)
				obs_data_set_bool(settings, pname,
						  carla_isEqual(param.value,
								param.max));
		} else if (param.hints & PARAMETER_IS_INTEGER) {
			prop = obs_properties_add_int_slider(
				props, pname, param.name, param.min, param.max,
				param.step);

			obs_data_set_default_int(settings, pname, param.def);

			if (param.unit.isNotEmpty())
				obs_property_int_set_suffix(prop, param.unit);

			if (reset)
				obs_data_set_int(settings, pname, param.value);
		} else {
			prop = obs_properties_add_float_slider(
				props, pname, param.name, param.min, param.max,
				param.step);

			obs_data_set_default_double(settings, pname, param.def);

			if (param.unit.isNotEmpty())
				obs_property_float_set_suffix(prop, param.unit);

			if (reset)
				obs_data_set_double(settings, pname,
						    param.value);
		}

		obs_property_set_modified_callback2(
			prop, carla_priv_param_changed, priv);
	}

	obs_data_release(settings);
}

// --------------------------------------------------------------------------------------------------------------------

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

	struct carla_priv *priv = static_cast<struct carla_priv *>(data);

	const char *filename = carla_qt_file_dialog(
		false, false, obs_module_text("Load File"), NULL);

	if (filename == NULL)
		return false;

	priv->bridge.cleanup();
	priv->bridge.init(priv->bufferSize, priv->sampleRate);

	// TODO put in the correct types
	// TODO show error message if bridge fails
	priv->bridge.start(PLUGIN_VST2, "x86_64",
			   "/usr/lib/carla/carla-bridge-native", "", filename,
			   0);

	return carla_post_load_callback(priv, props);
}

bool carla_priv_select_plugin_callback(obs_properties_t *props,
				       obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(property);

	struct carla_priv *priv = static_cast<struct carla_priv *>(data);

	const PluginListDialogResults *plugin =
		carla_frontend_createAndExecPluginListDialog(
			carla_qt_get_main_window());

	if (plugin == NULL)
		return false;

	priv->bridge.cleanup();
	priv->bridge.init(priv->bufferSize, priv->sampleRate);

	// TODO show error message if bridge fails
	priv->bridge.start((PluginType)plugin->type, "x86_64",
			   "/usr/lib/carla/carla-bridge-native", plugin->label,
			   plugin->filename, plugin->uniqueId);

	return carla_post_load_callback(priv, props);
}

bool carla_priv_show_gui_callback(obs_properties_t *props,
				  obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	struct carla_priv *priv = static_cast<struct carla_priv *>(data);

	priv->bridge.show_ui();

	return false;
}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_free(void *data)
{
	free(data);
}

// --------------------------------------------------------------------------------------------------------------------
