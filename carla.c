/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-wrapper.h"

// --------------------------------------------------------------------------------------------------------------------

struct carla_data {
    // carla host details, intentionally kept private so we can easily swap internals
    struct carla_priv *priv;

    // current OBS config
    bool isFilter;
    size_t channels;
    uint32_t sampleRate;

    // internal buffering
    uint32_t bufferPos;
    uint32_t bufferSize;
    float *abuffer1, *abuffer2;
};

// --------------------------------------------------------------------------------------------------------------------
// private methods

static void carla_obs_idle_callback(void *data, float unused)
{
    UNUSED_PARAMETER(unused);
    struct carla_data *carla = data;
    carla_obs_idle(carla->priv);
}

// --------------------------------------------------------------------------------------------------------------------
// obs plugin methods

static const char *carla_obs_get_name(void *data)
{
    struct carla_data *carla = data;

    return carla->isFilter ? obs_module_text("Audio Plugin Filter")
                           : obs_module_text("Audio Plugin Input");
}

static void *carla_obs_create(obs_data_t *settings, obs_source_t *source)
{
    TRACE_CALL

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

    struct carla_priv *priv = carla_obs_alloc(source, MAX_AUDIO_BUFFER_SIZE, sampleRate);
    if (carla == NULL)
        goto fail3;

    carla->priv = priv;
    carla->isFilter = false;
    carla->channels = channels;
    carla->sampleRate = sampleRate;

    carla->bufferPos = 0;
    carla->bufferSize = MAX_AUDIO_BUFFER_SIZE;

    obs_add_tick_callback(carla_obs_idle_callback, carla);

    return carla;

fail3:
    bfree(carla->abuffer1);

fail2:
    bfree(carla->abuffer1);

fail1:
    bfree(carla);
    return NULL;
}

static void carla_obs_destroy(void *data)
{
    TRACE_CALL
    struct carla_data *carla = data;
    obs_remove_tick_callback(carla_obs_idle_callback, carla);
    carla_obs_dealloc(carla->priv);
    bfree(carla->abuffer2);
    bfree(carla->abuffer1);
    bfree(carla);
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

static struct obs_audio_data *carla_obs_filter_audio(void *data, struct obs_audio_data *audio)
{
    struct carla_data *carla = data;
    const uint32_t frames = audio->frames;
    const uint32_t bufferSize = carla->bufferSize;
    uint32_t bufferPos = carla->bufferPos;
    float *carlabuffers[2] = { carla->abuffer1, carla->abuffer2 };
    float *obsbuffers[2] = { (float *)audio->data[0], (float *)audio->data[carla->channels >= 2 ? 1 : 0] };

    for (uint32_t i=0, j; i<frames; ++i)
    {
        j = bufferPos++;
        carlabuffers[0][j] = obsbuffers[0][i];
        carlabuffers[1][j] = obsbuffers[1][i];

        if (bufferPos == bufferSize)
        {
            bufferPos = 0;
            carla_obs_process_audio(carla->priv, carlabuffers, bufferSize);
        }

        obsbuffers[1][i] = carlabuffers[1][j];
        obsbuffers[0][i] = carlabuffers[0][j];
    }

    carla->bufferPos = bufferPos;

    return audio;
}

static obs_properties_t *carla_obs_get_properties(void *data)
{
    TRACE_CALL
    struct carla_data *carla = data;

    obs_properties_t *props = obs_properties_create();

    obs_properties_add_button2(props, PROP_SELECT_PLUGIN, obs_module_text("Select plugin..."),
                               carla_obs_select_plugin_callback, carla->priv);

    carla_obs_readd_properties(carla->priv, props);

    TRACE_CALL
    return props;
}

static void carla_obs_save(void *data, obs_data_t *settings)
{
    TRACE_CALL
    struct carla_data *carla = data;

    char *state = carla_obs_get_state(carla->priv);
    if (state)
    {
        obs_data_set_string(settings, "state", state);
        carla_obs_free(state);
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
        printf("got carla state:\n%s", state);
        carla_obs_set_state(carla->priv, state);
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
        .create = carla_obs_create,
        .destroy = carla_obs_destroy,
        .update = carla_obs_update,
        .filter_audio = carla_obs_filter_audio,
        .get_properties = carla_obs_get_properties,
        .save = carla_obs_save,
        .load = carla_obs_load,
    };
    obs_register_source(&filter);

    static const struct obs_source_info input = {
        .id = "carla_input",
        .type = OBS_SOURCE_TYPE_INPUT,
        .output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_MONITOR_BY_DEFAULT,
        .get_name = carla_obs_get_name,
        .create = carla_obs_create,
        .destroy = carla_obs_destroy,
        .update = carla_obs_update,
        .filter_audio = carla_obs_filter_audio,
        .get_properties = carla_obs_get_properties,
        .save = carla_obs_save,
        .load = carla_obs_load,
        .icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT,
    };
    obs_register_source(&input);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------
