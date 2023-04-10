/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <obs-module.h>
#include <util/platform.h>

#include "CarlaNativePlugin.h"

// generates a warning if this is defined as anything else
#define CARLA_API

// --------------------------------------------------------------------------------------------------------------------
// forward declarations of carla host methods

static uint32_t host_get_buffer_size(NativeHostHandle);
static double host_get_sample_rate(NativeHostHandle);
static bool host_is_offline(NativeHostHandle);
static const NativeTimeInfo* host_get_time_info(NativeHostHandle handle);
static bool host_write_midi_event(NativeHostHandle handle, const NativeMidiEvent* event);
static void host_ui_parameter_changed(NativeHostHandle handle, uint32_t index, float value);
static void host_ui_midi_program_changed(NativeHostHandle handle, uint8_t channel, uint32_t bank, uint32_t program);
static void host_ui_custom_data_changed(NativeHostHandle handle, const char* key, const char* value);
static void host_ui_closed(NativeHostHandle handle);
static const char* host_ui_open_file(NativeHostHandle handle, bool isDir, const char* title, const char* filter);
static const char* host_ui_save_file(NativeHostHandle handle, bool isDir, const char* title, const char* filter);
static intptr_t host_dispatcher(NativeHostHandle handle, NativeHostDispatcherOpcode opcode,
                                int32_t index, intptr_t value, void* ptr, float opt);

// --------------------------------------------------------------------------------------------------------------------
// carla + obs integration methods

struct carla_data {
    const NativePluginDescriptor* descriptor;
    NativePluginHandle handle;
    NativeHostDescriptor host;
    NativeTimeInfo timeInfo;
    CarlaHostHandle internalHostHandle;
    size_t channels;
    uint32_t bufferSize;
    uint32_t sampleRate;
    float* dummyBuffer;
};

static const char *carla_obs_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("Carla Plugin Host");
}

static void *carla_obs_create(obs_data_t *settings, obs_source_t *filter)
{
    const NativePluginDescriptor* descriptor = carla_get_native_rack_plugin();
    if (descriptor == NULL)
        return NULL;

    struct carla_data *carla = malloc(sizeof(*carla));
    if (carla == NULL)
        return NULL;

    carla->dummyBuffer = calloc(sizeof(float), 512);
    if (carla->dummyBuffer == NULL)
    {
        free(carla);
        return NULL;
    }

    const audio_t *audio = obs_get_audio();
    carla->channels = audio_output_get_channels(audio);
    carla->bufferSize = 512;
    carla->sampleRate = audio_output_get_sample_rate(audio);

    carla->descriptor = descriptor;

    {
        NativeHostDescriptor host = {
            .handle = carla,
            .resourceDir = carla_get_library_folder(),
            .uiName = "OBS",
            .get_buffer_size = host_get_buffer_size,
            .get_sample_rate = host_get_sample_rate,
            .is_offline = host_is_offline,
            .get_time_info = host_get_time_info,
            .write_midi_event = host_write_midi_event,
            .ui_parameter_changed = host_ui_parameter_changed,
            .ui_midi_program_changed = host_ui_midi_program_changed,
            .ui_custom_data_changed = host_ui_custom_data_changed,
            .ui_closed = host_ui_closed,
            .ui_open_file = host_ui_open_file,
            .ui_save_file = host_ui_save_file,
            .dispatcher = host_dispatcher
        };
        carla->host = host;
    }

    {
        NativeTimeInfo timeInfo = {
            .usecs = os_gettime_ns() * 1000,
        };
        carla->timeInfo = timeInfo;
    }

    carla->handle = descriptor->instantiate(&carla->host);
    if (carla->handle == NULL)
        goto fail;

    carla->internalHostHandle = carla_create_native_plugin_host_handle(descriptor, carla->handle);
    if (carla->internalHostHandle == NULL)
        goto fail;

    descriptor->activate(carla->handle);

    return carla;

fail:
    if (carla->handle != NULL)
        descriptor->cleanup(carla->handle);

    bfree(carla);
    return NULL;
}

static void carla_obs_destroy(void *data)
{
    struct carla_data *carla = data;
    carla_host_handle_free(carla->internalHostHandle);
    carla->descriptor->deactivate(carla->handle);
    carla->descriptor->cleanup(carla->handle);
    free(carla->dummyBuffer);
    bfree(carla);
}

static void carla_obs_update(void *data, obs_data_t *settings)
{
}

static struct obs_audio_data *carla_obs_filter_audio(void *data, struct obs_audio_data *audio)
{
    struct carla_data *carla = data;
    const uint32_t frames = audio->frames;

    float* abuffers[MAX_AV_PLANES];
    {
        size_t c = 0;
        for (; c < carla->channels; c++)
            abuffers[c] = (float *)audio->data[c];
        for (; c < 2; c++)
            abuffers[c] = carla->dummyBuffer;
    }

    carla->descriptor->process(carla->handle, abuffers, abuffers, frames, NULL, 0);
    return audio;
}

static void carla_obs_get_defaults(obs_data_t *defaults)
{
}

static bool open_editor_button_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    struct carla_data *carla = data;

    // TODO open plugin list dialog

    return true;
}

static obs_properties_t *carla_obs_get_properties(void *unused)
{
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_button(props, "plugin-add", obs_module_text("Add plugin..."), open_editor_button_clicked);
    return props;
}

static void carla_obs_save(void *data, obs_data_t *settings)
{
}

// --------------------------------------------------------------------------------------------------------------------
// carla host methods

static uint32_t host_get_buffer_size(const NativeHostHandle handle)
{
    const struct carla_data *carla = handle;
    return carla->bufferSize;
}

static double host_get_sample_rate(const NativeHostHandle handle)
{
    const struct carla_data *carla = handle;
    return carla->sampleRate;
}

static bool host_is_offline(NativeHostHandle)
{
    return false;
}

static const NativeTimeInfo* host_get_time_info(const NativeHostHandle handle)
{
    const struct carla_data *carla = handle;
    return &carla->timeInfo;
}

static bool host_write_midi_event(const NativeHostHandle handle, const NativeMidiEvent* const event)
{
    return false;
}

static void host_ui_parameter_changed(const NativeHostHandle handle, const uint32_t index, const float value)
{
}

static void host_ui_midi_program_changed(NativeHostHandle handle, uint8_t channel, uint32_t bank, uint32_t program)
{
}

static void host_ui_custom_data_changed(NativeHostHandle handle, const char* key, const char* value)
{
}

static void host_ui_closed(NativeHostHandle handle)
{
}

static const char* host_ui_open_file(const NativeHostHandle handle, const bool isDir, const char* const title, const char* const filter)
{
    return NULL;
}

static const char* host_ui_save_file(NativeHostHandle, bool isDir, const char* title, const char* filter)
{
    return NULL;
}

static intptr_t host_dispatcher(const NativeHostHandle handle, const NativeHostDispatcherOpcode opcode,
                                const int32_t index, const intptr_t value, void* const ptr, const float opt)
{
    return 0;
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
    static struct obs_source_info filter = {};
    filter.id = "carla_filter";
    filter.type = OBS_SOURCE_TYPE_FILTER;
    filter.output_flags = OBS_SOURCE_AUDIO;
    filter.get_name = carla_obs_get_name;
    filter.create = carla_obs_create;
    filter.destroy = carla_obs_destroy;
    filter.update = carla_obs_update;
    filter.filter_audio = carla_obs_filter_audio;
    // filter.get_defaults = carla_obs_get_defaults,
    filter.get_properties = carla_obs_get_properties;
    filter.save = carla_obs_save;

    obs_register_source(&filter);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------
