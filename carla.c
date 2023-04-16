/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-wrapper.h"

// maximum buffer used, could be lower
#define MAX_AUDIO_BUFFER_SIZE 512

// --------------------------------------------------------------------------------------------------------------------

enum buffer_size_mode {
    buffer_size_dynamic,
    buffer_size_static_128,
    buffer_size_static_256,
    buffer_size_static_512,
    buffer_size_static_max = buffer_size_static_512
};

struct carla_data {
    // carla host details, intentionally kept private so we can easily swap internals
    struct carla_priv *priv;

    // current OBS config
    size_t channels;
    uint32_t sampleRate;

    // internal buffering
    enum buffer_size_mode bufferSizeMode;
    uint32_t bufferPos;
    float *abuffer1, *abuffer2;
};

// --------------------------------------------------------------------------------------------------------------------
// private methods

static void carla_obs_idle_callback(void *data, float unused)
{
    UNUSED_PARAMETER(unused);
    struct carla_data *carla = data;
    carla_priv_idle(carla->priv);
}

// --------------------------------------------------------------------------------------------------------------------
// obs plugin methods

static const char *carla_obs_get_name(void *data)
{
    return !strcmp(data, "filter") ? obs_module_text("Audio Plugin Filter")
                                   : obs_module_text("Audio Plugin Input");
}

static void *carla_obs_create(obs_data_t *settings, obs_source_t *source, bool isFilter)
{
    const audio_t *audio = obs_get_audio();
    const size_t channels = audio_output_get_channels(audio);
    const uint32_t sampleRate = audio_output_get_sample_rate(audio);

    if (channels == 0 || sampleRate == 0)
        return NULL;

    struct carla_data *carla = bzalloc(sizeof(*carla));
    if (carla == NULL)
        return NULL;

    carla->abuffer1 = bzalloc(sizeof(float) * MAX_AUDIO_BUFFER_SIZE);
    if (carla->abuffer1 == NULL)
        goto fail1;

    carla->abuffer2 = bzalloc(sizeof(float) * MAX_AUDIO_BUFFER_SIZE);
    if (carla->abuffer2 == NULL)
        goto fail2;

    struct carla_priv *priv = carla_priv_create(source, isFilter ? MAX_AUDIO_BUFFER_SIZE : 0, sampleRate);
    if (carla == NULL)
        goto fail3;

    carla->priv = priv;
    carla->channels = channels;
    carla->sampleRate = sampleRate;

    carla->bufferPos = 0;
    carla->bufferSizeMode = buffer_size_dynamic;

    obs_add_tick_callback(carla_obs_idle_callback, carla);

    return carla;

fail3:
    bfree(carla->abuffer2);

fail2:
    bfree(carla->abuffer1);

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
    carla_priv_destroy(carla->priv);
    bfree(carla->abuffer2);
    bfree(carla->abuffer1);
    bfree(carla);
}

static obs_properties_t *carla_obs_get_properties(void *data)
{
    TRACE_CALL
    struct carla_data *carla = data;

    obs_properties_t *props = obs_properties_create();

    obs_properties_add_button2(props, PROP_SELECT_PLUGIN, obs_module_text("Select plugin..."),
                               carla_priv_select_plugin_callback, carla->priv);

    carla_priv_readd_properties(carla->priv, props);

    TRACE_CALL
    return props;
}

static void carla_obs_update(void *data, obs_data_t *settings)
{
    TRACE_CALL
//     struct carla_data *carla = data;
    // carla->isUpdating = true;
    // const char *json = obs_data_get_json_pretty(settings);

    printf("carla_obs_update called for:");

    for (obs_data_item_t *item = obs_data_first(settings); item != NULL; obs_data_item_next(&item))
    {
         printf(" %s,", obs_data_item_get_name(item));
    }

    printf("\n");
    TRACE_CALL
}

static void carla_obs_activate(void *data)
{
    struct carla_data *carla = data;
    carla_priv_activate(carla->priv);
}

static void carla_obs_deactivate(void *data)
{
    struct carla_data *carla = data;
    carla_priv_deactivate(carla->priv);
}

static void carla_obs_filter_audio_dynamic(struct carla_data *carla, struct obs_audio_data *audio)
{
    float *obsbuffers[MAX_AV_PLANES];
    {
        for (uint32_t i=0; i<MAX_AV_PLANES; ++i)
            obsbuffers[i] = (float *)audio->data[i];
    }

    carla_priv_process_audio(carla->priv, obsbuffers, audio->frames);
}

static void carla_obs_filter_audio_static(struct carla_data *carla, struct obs_audio_data *audio)
{
    const uint32_t bufferSize = carla->bufferSizeMode == buffer_size_static_128 ? 128
                              : carla->bufferSizeMode == buffer_size_static_256 ? 256
                              : MAX_AUDIO_BUFFER_SIZE;

    /* TODO
    float *carlabuffers[2] = { carla->abuffer1, carla->abuffer2 };
    float *obsbuffers[MAX_AV_PLANES];
    {
        for (uint32_t i=0; i<MAX_AV_PLANES; ++i)
            obsbuffers[i] = (float *)audio->data[i];
        for (uint32_t i=0; i<MAX_AV_PLANES; ++i)
            obsbuffers[i] = (float *)audio->data[i];
    }

    uint32_t bufferPos = carla->bufferPos;

    for (uint32_t i=0, j; i<frames; ++i)
    {
        j = bufferPos++;
        carlabuffers[0][j] = obsbuffers[0][i];
        carlabuffers[1][j] = obsbuffers[1][i];

        if (bufferPos == bufferSize)
        {
            bufferPos = 0;
            carla_priv_process_audio(carla->priv, carlabuffers, bufferSize);
        }

        obsbuffers[1][i] = carlabuffers[1][j];
        obsbuffers[0][i] = carlabuffers[0][j];
    }

    carla->bufferPos = bufferPos;
    */
}

static struct obs_audio_data *carla_obs_filter_audio(void *data, struct obs_audio_data *audio)
{
    struct carla_data *carla = data;

    switch (carla->bufferSizeMode)
    {
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
    TRACE_CALL
    struct carla_data *carla = data;

    char *state = carla_priv_get_state(carla->priv);
    if (state)
    {
        obs_data_set_string(settings, "state", state);
        carla_priv_free(state);
    }

    TRACE_CALL
}

static void carla_obs_load(void *data, obs_data_t *settings)
{
    TRACE_CALL
    struct carla_data *carla = data;

    const char *state = obs_data_get_string(settings, "state");
    if (state)
    {
        // printf("got carla state:\n%s", state);
        carla_priv_set_state(carla->priv, state);
    }

    TRACE_CALL
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
        .update = carla_obs_update,
//         .activate = carla_obs_activate,
//         .deactivate = carla_obs_deactivate,
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
        .update = carla_obs_update,
//         .activate = carla_obs_activate,
//         .deactivate = carla_obs_deactivate,
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
