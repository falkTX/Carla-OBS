/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-wrapper.h"
#include <util/platform.h>

// for audio generator thread
#include <threads.h>

// --------------------------------------------------------------------------------------------------------------------

struct carla_data {
	// carla host details, intentionally kept private so we can easily swap internals
	struct carla_priv *priv;

	// current OBS config
	bool activated;
	size_t channels;
	uint32_t sample_rate;
	obs_source_t *source;

	// audio generator thread
	bool audiogen_enabled;
	volatile bool audiogen_running;
	thrd_t audiogen_thread;

	// internal buffering
	float *buffers[MAX_AV_PLANES];
	uint16_t buffer_head;
	uint16_t buffer_tail;
	enum buffer_size_mode buffer_size_mode;
};

// --------------------------------------------------------------------------------------------------------------------
// private methods

static int carla_obs_audio_gen_thread(void *data)
{
	struct carla_data *carla = data;

	struct obs_source_audio out = {
		.speakers = SPEAKERS_STEREO,
		.format = AUDIO_FORMAT_FLOAT_PLANAR,
		.samples_per_sec = carla->sample_rate,
	};

	for (uint8_t c = 0; c < MAX_AV_PLANES; ++c)
		out.data[c] = (const uint8_t *)carla->buffers[c];

	const uint32_t sample_rate = carla->sample_rate;

	uint64_t start_time = out.timestamp = os_gettime_ns();
	uint64_t total_samples = 0;

	while (carla->audiogen_running) {
		const uint32_t buffer_size =
			carla->buffer_size_mode == buffer_size_static_128 ? 128 :
			carla->buffer_size_mode == buffer_size_static_256 ? 256 :
			MAX_AUDIO_BUFFER_SIZE;

		out.frames = buffer_size;
		carla_priv_process_audio(carla->priv, carla->buffers,
					 buffer_size);
		obs_source_output_audio(carla->source, &out);

		if (!carla->audiogen_running)
			break;

		total_samples += buffer_size;
		out.timestamp = start_time +
				audio_frames_to_ns(sample_rate, total_samples);

		os_sleepto_ns_fast(out.timestamp);
	}

	return thrd_success;
}

static void carla_obs_idle_callback(void *data, float unused)
{
	UNUSED_PARAMETER(unused);
	struct carla_data *carla = data;
	carla_priv_idle(carla->priv);
}

// --------------------------------------------------------------------------------------------------------------------
// obs plugin methods

static void carla_obs_activate(void *data);
static void carla_obs_deactivate(void *data);

static const char *carla_obs_get_name(void *data)
{
	return !strcmp(data, "filter")
		       ? obs_module_text("Carla Plugin Host (Filter)")
		       : obs_module_text("Carla Plugin Host (Input)");
}

static void *carla_obs_create(obs_data_t *settings, obs_source_t *source,
			      bool isFilter)
{
	UNUSED_PARAMETER(settings);

	const audio_t *audio = obs_get_audio();
	const size_t channels = audio_output_get_channels(audio);
	const uint32_t sample_rate = audio_output_get_sample_rate(audio);

	if (channels == 0 || sample_rate == 0)
		return NULL;

	struct carla_data *carla = bzalloc(sizeof(*carla));
	if (carla == NULL)
		return NULL;

	for (uint8_t c = 0; c < MAX_AV_PLANES; ++c) {
		carla->buffers[c] =
			bzalloc(sizeof(float) * MAX_AUDIO_BUFFER_SIZE);
		if (carla->buffers[c] == NULL)
			goto fail1;
	}

	struct carla_priv *priv =
		carla_priv_create(source, buffer_size_dynamic, sample_rate);
	if (carla == NULL)
		goto fail2;

	carla->priv = priv;
	carla->source = source;
	carla->channels = channels;
	carla->sample_rate = sample_rate;

	carla->buffer_head = 0;
	carla->buffer_tail = UINT16_MAX;
	carla->buffer_size_mode = buffer_size_dynamic;

	if (!isFilter) {
		carla->audiogen_enabled = true;
	}

	obs_add_tick_callback(carla_obs_idle_callback, carla);

	carla_obs_activate(carla);

	return carla;

fail2:
	for (uint8_t c = 0; c < MAX_AV_PLANES; ++c)
		bfree(carla->buffers[c]);

fail1:
	bfree(carla);
	return NULL;
}

static void *carla_obs_create_filter(obs_data_t *settings, obs_source_t *source)
{
	return carla_obs_create(settings, source, true);
}

static void *carla_obs_create_input(obs_data_t *settings, obs_source_t *source)
{
	return carla_obs_create(settings, source, false);
}

static void carla_obs_destroy(void *data)
{
	struct carla_data *carla = data;
	obs_remove_tick_callback(carla_obs_idle_callback, carla);

	if (carla->activated)
		carla_obs_deactivate(carla);

	carla_priv_destroy(carla->priv);
	for (uint8_t c = 0; c < MAX_AV_PLANES; ++c)
		bfree(carla->buffers[c]);
	bfree(carla);
}

static obs_properties_t *carla_obs_get_properties(void *data)
{
	struct carla_data *carla = data;

	obs_properties_t *props = obs_properties_create();

	obs_properties_add_button2(props, PROP_SELECT_PLUGIN,
				   obs_module_text("Select plugin..."),
				   carla_priv_select_plugin_callback,
				   carla->priv);

	obs_properties_add_button2(props, PROP_LOAD_FILE,
				   obs_module_text("Load file..."),
				   carla_priv_load_file_callback, carla->priv);

	carla_priv_readd_properties(carla->priv, props, false);

	return props;
}

static void carla_obs_activate(void *data)
{
	struct carla_data *carla = data;
	assert(!carla->activated);
	carla->activated = true;

	carla_priv_activate(carla->priv);

	if (carla->audiogen_enabled) {
		assert(!carla->audiogen_running);
		carla->audiogen_running = true;
		thrd_create(&carla->audiogen_thread, carla_obs_audio_gen_thread,
			    carla);
	}
}

static void carla_obs_deactivate(void *data)
{
	struct carla_data *carla = data;
	assert(carla->activated);
	carla->activated = false;

	if (carla->audiogen_running) {
		carla->audiogen_running = false;
		thrd_join(carla->audiogen_thread, NULL);
	}

	carla_priv_deactivate(carla->priv);
}

static void carla_obs_filter_audio_dynamic(struct carla_data *carla,
					   struct obs_audio_data *audio)
{
	float *obsbuffers[MAX_AV_PLANES];

	for (uint8_t c = 0; c < MAX_AV_PLANES; ++c)
		obsbuffers[c] = (float *)audio->data[c];

	carla_priv_process_audio(carla->priv, obsbuffers, audio->frames);
}

static void carla_obs_filter_audio_static(struct carla_data *carla,
					  struct obs_audio_data *audio)
{
	const uint32_t buffer_size =
		carla->buffer_size_mode == buffer_size_static_128 ? 128 :
		carla->buffer_size_mode == buffer_size_static_256 ? 256 :
		MAX_AUDIO_BUFFER_SIZE;
	const size_t channels = carla->channels;
	const uint32_t frames = audio->frames;

	// cast audio buffers to correct type
	float *obsbuffers[MAX_AV_PLANES];

	for (uint8_t c = 0; c < MAX_AV_PLANES; ++c)
		obsbuffers[c] = (float *)audio->data[c];

	// preload some variables before looping section
	uint16_t buffer_head = carla->buffer_head;
	uint16_t buffer_tail = carla->buffer_tail;

	for (uint32_t i = 0, h, t; i < frames; ++i) {
		// OBS -> plugin internal buffering
		h = buffer_head++;

		for (uint8_t c = 0; c < channels; ++c)
			carla->buffers[c][h] = obsbuffers[c][i];

		// when we reach the target buffer size, do audio processing
		if (buffer_head == buffer_size) {
			buffer_head = 0;
			carla_priv_process_audio(carla->priv, carla->buffers,
						 buffer_size);

			// we can now begin to copy back the buffer into OBS
			if (buffer_tail == UINT16_MAX)
				buffer_tail = 0;
		}

		if (buffer_tail == UINT16_MAX) {
			// buffering still taking place, skip until first audio cycle
			for (uint8_t c = 0; c < channels; ++c)
				obsbuffers[c][i] = 0.f;
		} else {
			// plugin -> OBS buffer copy
			t = buffer_tail++;

			for (uint8_t c = 0; c < channels; ++c)
				obsbuffers[c][i] = carla->buffers[c][t];

			if (buffer_tail == buffer_size)
				buffer_tail = 0;
		}
	}

	carla->buffer_head = buffer_head;
	carla->buffer_tail = buffer_tail;
}

static struct obs_audio_data *
carla_obs_filter_audio(void *data, struct obs_audio_data *audio)
{
	struct carla_data *carla = data;

	switch (carla->buffer_size_mode) {
	case buffer_size_dynamic:
		carla_obs_filter_audio_dynamic(carla, audio);
		break;
	case buffer_size_static_128:
	case buffer_size_static_256:
	case buffer_size_static_512:
		carla_obs_filter_audio_static(carla, audio);
		break;
	}

	return audio;
}

static void carla_obs_save(void *data, obs_data_t *settings)
{
	struct carla_data *carla = data;

	char *state = carla_priv_get_state(carla->priv);
	if (state) {
		obs_data_set_string(settings, "state", state);
		carla_priv_free(state);
	}
}

static void carla_obs_load(void *data, obs_data_t *settings)
{
	struct carla_data *carla = data;

	const char *state = obs_data_get_string(settings, "state");
	if (state)
		carla_priv_set_state(carla->priv, state);
}

// --------------------------------------------------------------------------------------------------------------------

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("carla", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "Carla Plugin Host";
}

bool obs_module_load(void)
{
	static const struct obs_source_info filter = {
		.id = "carla_filter",
		.type = OBS_SOURCE_TYPE_FILTER,
		.output_flags = OBS_SOURCE_AUDIO,
		.get_name = carla_obs_get_name,
		.create = carla_obs_create_filter,
		.destroy = carla_obs_destroy,
		// get_width, get_height, get_defaults
		.get_properties = carla_obs_get_properties,
		// update
		.activate = carla_obs_activate,
		.deactivate = carla_obs_deactivate,
		// show, hide, video_tick, video_render, filter_video
		.filter_audio = carla_obs_filter_audio,
		// enum_active_sources
		.save = carla_obs_save,
		.load = carla_obs_load,
		// mouse_click, mouse_move, mouse_wheel, focus, key_click, filter_remove,
		.type_data = "filter",
		// free_type_data
		// audio_render, enum_all_sources, transition_start, transition_stop, get_defaults2, audio_mix
		.icon_type = OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT,
		// media_play_pause, media_restart, media_stop, media_next, media_previous
		// media_get_duration, media_get_time, media_set_time, media_get_state
		// version, unversioned_id, missing_files, video_get_color_space
	};
	obs_register_source(&filter);

	static const struct obs_source_info input = {
		.id = "carla_input",
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_AUDIO,
		.get_name = carla_obs_get_name,
		.create = carla_obs_create_input,
		.destroy = carla_obs_destroy,
		// get_width, get_height, get_defaults
		.get_properties = carla_obs_get_properties,
		// update
		.activate = carla_obs_activate,
		.deactivate = carla_obs_deactivate,
		// show, hide, video_tick, video_render, filter_video, filter_audio, enum_active_sources
		.save = carla_obs_save,
		.load = carla_obs_load,
		// mouse_click, mouse_move, mouse_wheel, focus, key_click, filter_remove,
		.type_data = "input",
		// free_type_data
		// audio_render, enum_all_sources, transition_start, transition_stop, get_defaults2, audio_mix
		.icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT,
		// media_play_pause, media_restart, media_stop, media_next, media_previous
		// media_get_duration, media_get_time, media_set_time, media_get_state
		// version, unversioned_id, missing_files, video_get_color_space
	};
	obs_register_source(&input);

	return true;
}

// --------------------------------------------------------------------------------------------------------------------
