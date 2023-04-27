/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-wrapper.h"
#include "qtutils.h"
#include <util/platform.h>

// for audio generator thread
#include <threads.h>

// IDE helpers, must match cmake config
#define CARLA_PLUGIN_BUILD 1
#define HAVE_X11 1
#define REAL_BUILD 1
#define STATIC_PLUGIN_TARGET 1

#include "CarlaNativePlugin.h"
#include "CarlaFrontend.h"

// generates a warning if this is defined as anything else
#define CARLA_API

// --------------------------------------------------------------------------------------------------------------------
// helper methods

struct carla_main_thread_param_change {
    const NativePluginDescriptor* descriptor;
    NativePluginHandle handle;
    uint32_t index;
    float value;
};

static void carla_main_thread_param_change(void *data)
{
    struct carla_main_thread_param_change *priv = data;
    priv->descriptor->ui_set_parameter_value(priv->handle, priv->index, priv->value);
    bfree(data);
}

// --------------------------------------------------------------------------------------------------------------------
// private data methods

struct carla_param_data {
    uint32_t hints;
    float min, max;
};

struct carla_priv {
    obs_source_t *source;
    uint32_t bufferSize;
    double sampleRate;
    const NativePluginDescriptor* descriptor;
    NativePluginHandle handle;
    NativeHostDescriptor host;
    NativeTimeInfo timeInfo;
    CarlaHostHandle internalHostHandle;

    // cached parameter info
    uint32_t paramCount;
    struct carla_param_data* paramDetails;

    // update properties when timeout is reached, 0 means do nothing
    uint64_t update_requested;

    // keep track of active state
    volatile bool activated;

    // audio generator thread
    volatile bool audiogen_running;
    thrd_t audiogen_thread;
};

static int carla_audio_gen_thread(void *data)
{
    struct carla_priv *priv = data;

    float silence1[MAX_AUDIO_BUFFER_SIZE] = {0.f};
    float silence2[MAX_AUDIO_BUFFER_SIZE] = {0.f};
    float* silences[2] = {silence1,silence2};

    float buf1[MAX_AUDIO_BUFFER_SIZE] = {0.f};
    float buf2[MAX_AUDIO_BUFFER_SIZE] = {0.f};
    float* bufs[2] = {buf1,buf2};

    struct obs_source_audio out = {
        .data = {(uint8_t *)buf1,(uint8_t *)buf2,NULL},
        .speakers = SPEAKERS_STEREO,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .samples_per_sec = priv->sampleRate,
    };

    uint64_t now, prev, diff;
    prev = now = os_gettime_ns();

    while (priv->audiogen_running)
    {
        const uint64_t slice = priv->bufferSize * 1000000000ULL / priv->sampleRate;

        prev = now = os_gettime_ns();

        if (priv->activated)
        {
            out.frames = priv->bufferSize;
            out.timestamp = now;
            priv->timeInfo.usecs = now / 1000;
            priv->descriptor->process(priv->handle, silences, bufs, out.frames, NULL, 0);
            obs_source_output_audio(priv->source, &out);
            now = os_gettime_ns();
        }

        // time went backwards!
        if (now < prev)
            continue;

        diff = now - prev;

        // xrun!
        if (slice <= diff)
            continue;

        const uint64_t target = slice - diff;
        const struct timespec timeout = {
            target / 1000000000ULL,
            target % 1000000000ULL
        };
        thrd_sleep(&timeout, NULL);
    }

    return thrd_success;
}

// --------------------------------------------------------------------------------------------------------------------
// carla host methods

static uint32_t host_get_buffer_size(NativeHostHandle handle)
{
    const struct carla_priv *priv = handle;
    return priv->bufferSize;
}

static double host_get_sample_rate(NativeHostHandle handle)
{
    const struct carla_priv *priv = handle;
    return priv->sampleRate;
}

static bool host_is_offline(NativeHostHandle handle)
{
    UNUSED_PARAMETER(handle);
    return false;
}

static const NativeTimeInfo* host_get_time_info(NativeHostHandle handle)
{
    const struct carla_priv *priv = handle;
    return &priv->timeInfo;
}

static bool host_write_midi_event(NativeHostHandle handle, const NativeMidiEvent *event)
{
    UNUSED_PARAMETER(handle);
    UNUSED_PARAMETER(event);
    return false;
}

static void host_ui_parameter_changed(NativeHostHandle handle, uint32_t index, float value)
{
    struct carla_priv *priv = handle;

    if (index >= priv->paramCount)
        return;

    // skip parameters that we do not show
    const uint32_t hints = priv->paramDetails[index].hints;
    if ((hints & NATIVE_PARAMETER_IS_ENABLED) == 0)
        return;
    if (hints & NATIVE_PARAMETER_IS_OUTPUT)
        return;

    char pname[PARAM_NAME_SIZE] = PARAM_NAME_INIT;
    param_index_to_name(index, pname);

    obs_source_t *source = priv->source;
    obs_data_t *settings = obs_source_get_settings(source);

    /**/ if (hints & NATIVE_PARAMETER_IS_BOOLEAN)
        obs_data_set_bool(settings, pname, value > 0.5f ? 1.f : 0.f);
    else if (hints & NATIVE_PARAMETER_IS_INTEGER)
        obs_data_set_int(settings, pname, value);
    else
        obs_data_set_double(settings, pname, value);

    obs_data_release(settings);

    priv->update_requested = os_gettime_ns();
}

static void host_ui_midi_program_changed(NativeHostHandle handle, uint8_t channel, uint32_t bank, uint32_t program)
{
    UNUSED_PARAMETER(handle);
    UNUSED_PARAMETER(channel);
    UNUSED_PARAMETER(bank);
    UNUSED_PARAMETER(program);
}

static void host_ui_custom_data_changed(NativeHostHandle handle, const char *key, const char *value)
{
    UNUSED_PARAMETER(handle);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(value);
}

static void host_ui_closed(NativeHostHandle handle)
{
    UNUSED_PARAMETER(handle);
}

static const char* host_ui_open_file(NativeHostHandle handle, bool isDir, const char *title, const char *filter)
{
    UNUSED_PARAMETER(handle);
    return carla_qt_file_dialog(false, isDir, title, filter);
}

static const char* host_ui_save_file(NativeHostHandle handle, bool isDir, const char *title, const char *filter)
{
    UNUSED_PARAMETER(handle);
    return carla_qt_file_dialog(true, isDir, title, filter);
}

static intptr_t host_dispatcher(NativeHostHandle handle, NativeHostDispatcherOpcode opcode,
                                int32_t index, intptr_t value, void *ptr, float opt)
{
    UNUSED_PARAMETER(handle);
    UNUSED_PARAMETER(index);
    UNUSED_PARAMETER(value);
    UNUSED_PARAMETER(ptr);
    UNUSED_PARAMETER(opt);

    switch (opcode)
    {
    case NATIVE_HOST_OPCODE_NULL:
    case NATIVE_HOST_OPCODE_UPDATE_PARAMETER:
    case NATIVE_HOST_OPCODE_UPDATE_MIDI_PROGRAM:
    case NATIVE_HOST_OPCODE_RELOAD_PARAMETERS:
    case NATIVE_HOST_OPCODE_RELOAD_MIDI_PROGRAMS:
    case NATIVE_HOST_OPCODE_RELOAD_ALL:
    case NATIVE_HOST_OPCODE_UI_UNAVAILABLE:
    case NATIVE_HOST_OPCODE_HOST_IDLE:
        break;
    case NATIVE_HOST_OPCODE_INTERNAL_PLUGIN:
    case NATIVE_HOST_OPCODE_QUEUE_INLINE_DISPLAY:
    case NATIVE_HOST_OPCODE_UI_TOUCH_PARAMETER:
    case NATIVE_HOST_OPCODE_REQUEST_IDLE:
    case NATIVE_HOST_OPCODE_GET_FILE_PATH:
    case NATIVE_HOST_OPCODE_UI_RESIZE:
    case NATIVE_HOST_OPCODE_PREVIEW_BUFFER_DATA:
        break;
    }

    return 0;
}

// --------------------------------------------------------------------------------------------------------------------
// carla + obs integration methods

struct carla_priv *carla_priv_create(obs_source_t *source, enum buffer_size_mode bufsize, uint32_t srate, bool filter)
{
    const NativePluginDescriptor* descriptor = carla_get_native_rack_plugin();
    if (descriptor == NULL)
        return NULL;

    struct carla_priv *priv = bzalloc(sizeof(struct carla_priv));
    if (priv == NULL)
        return NULL;

    priv->source = source;
    priv->bufferSize = bufsize_mode_to_frames(bufsize);
    priv->sampleRate = srate;
    priv->descriptor = descriptor;

    assert(priv->bufferSize != 0);
    if (priv->bufferSize == 0)
        goto fail1;

    {
        NativeHostDescriptor host = {
            .handle = priv,
            .resourceDir = carla_get_library_folder(),
            .uiName = "OBS",
            .uiParentId = 0,
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
        priv->host = host;
    }

    {
        NativeTimeInfo timeInfo = {
            .usecs = os_gettime_ns() / 1000,
        };
        priv->timeInfo = timeInfo;
    }

    priv->handle = descriptor->instantiate(&priv->host);
    if (priv->handle == NULL)
        goto fail1;

    priv->internalHostHandle = carla_create_native_plugin_host_handle(descriptor, priv->handle);
    if (priv->internalHostHandle == NULL)
        goto fail2;

    descriptor->dispatcher(priv->handle, NATIVE_PLUGIN_OPCODE_HOST_USES_EMBED, 0, 0, NULL, 0.f);

    // TODO build and setup local bridges
    carla_set_engine_option(priv->internalHostHandle, ENGINE_OPTION_PATH_BINARIES, 0, "/usr/lib/carla");
    carla_set_engine_option(priv->internalHostHandle, ENGINE_OPTION_PATH_RESOURCES, 0, "/usr/share/carla");
    carla_set_engine_option(priv->internalHostHandle, ENGINE_OPTION_PREFER_PLUGIN_BRIDGES, 1, NULL);

    if (!filter)
    {
        priv->audiogen_running = true;
        thrd_create(&priv->audiogen_thread, carla_audio_gen_thread, priv);
    }

    return priv;

fail2:
    descriptor->cleanup(&priv->host);

fail1:
    bfree(priv);
    return NULL;
}

void carla_priv_destroy(struct carla_priv *priv)
{
    if (priv->activated)
        carla_priv_deactivate(priv);

    carla_host_handle_free(priv->internalHostHandle);
    priv->descriptor->cleanup(priv->handle);
    bfree(priv->paramDetails);
    bfree(priv);
}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_activate(struct carla_priv *priv)
{
    assert(!priv->activated);
    priv->descriptor->activate(priv->handle);
    priv->activated = true;
}

void carla_priv_deactivate(struct carla_priv *priv)
{
    assert(priv->activated);
    priv->activated = false;

    if (priv->audiogen_running)
    {
        priv->audiogen_running = false;
        thrd_join(priv->audiogen_thread, NULL);
    }

    priv->descriptor->deactivate(priv->handle);
}

void carla_priv_process_audio(struct carla_priv *priv, float *buffers[2], uint32_t frames)
{
    priv->timeInfo.usecs = os_gettime_ns() / 1000;
    priv->descriptor->process(priv->handle, buffers, buffers, frames, NULL, 0);
}

void carla_priv_idle(struct carla_priv *priv)
{
    priv->descriptor->ui_idle(priv->handle);

    if (priv->update_requested != 0)
    {
        const uint64_t now = os_gettime_ns();

        // request in the future?
        if (now < priv->update_requested)
        {
            priv->update_requested = now;
            return;
        }

        if (now - priv->update_requested >= 100000000ULL) // 100ms
        {
            priv->update_requested = 0;

            signal_handler_t *sighandler = obs_source_get_signal_handler(priv->source);
            signal_handler_signal(sighandler, "update_properties", NULL);
        }
    }
}

char *carla_priv_get_state(struct carla_priv *priv)
{
    return priv->descriptor->get_state(priv->handle);
}

void carla_priv_set_state(struct carla_priv *priv, const char *state)
{
    priv->descriptor->set_state(priv->handle, state);
}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_set_buffer_size(struct carla_priv *priv, enum buffer_size_mode bufsize)
{
    const uint32_t new_buffer_size = bufsize_mode_to_frames(bufsize);
    assert(new_buffer_size != 0);
    if (new_buffer_size == 0)
        return;

    const bool activated = priv->activated;

    if (activated)
        carla_priv_deactivate(priv);

    priv->bufferSize = new_buffer_size;
    priv->descriptor->dispatcher(priv->handle,
                                 NATIVE_PLUGIN_OPCODE_BUFFER_SIZE_CHANGED, new_buffer_size,
                                 0, NULL, 0.f);

    if (activated)
        carla_priv_activate(priv);
}

// --------------------------------------------------------------------------------------------------------------------

static bool carla_priv_param_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(props);

    struct carla_priv *priv = data;

    const char *const pname = obs_property_name(property);
    if (pname == NULL)
        return false;

    const char* pname2 = pname + 1;
    while (*pname2 == '0')
        ++pname2;

    const int pindex = atoi(pname2);

    if (pindex < 0 || pindex >= (int)priv->paramCount)
        return false;

    const float min = priv->paramDetails[pindex].min;
    const float max = priv->paramDetails[pindex].max;

    float value;
    switch (obs_property_get_type(property))
    {
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
    priv->descriptor->set_parameter_value(priv->handle, pindex, value);

    // UI param change notification needs to happen on main thread
    struct carla_main_thread_param_change mchange = {
        .descriptor = priv->descriptor,
        .handle = priv->handle,
        .index = pindex,
        .value = value
    };
    struct carla_main_thread_param_change *mchangeptr = bmalloc(sizeof(mchange));
    *mchangeptr = mchange;
    carla_qt_callback_on_main_thread(carla_main_thread_param_change, mchangeptr);

    return false;
}

void carla_priv_readd_properties(struct carla_priv *priv, obs_properties_t *props, bool reset)
{
    obs_data_t *settings = obs_source_get_settings(priv->source);

    // show/hide GUI button
    if (carla_get_current_plugin_count(priv->internalHostHandle) != 0 &&
        carla_get_plugin_info(priv->internalHostHandle, 0)->hints & PLUGIN_HAS_CUSTOM_UI)
    {
        obs_properties_add_button2(props, PROP_SHOW_GUI, obs_module_text("Show custom GUI"),
                                   carla_priv_show_gui_callback, priv);
    }

    uint32_t params = priv->descriptor->get_parameter_count(priv->handle);
    if (params > CARLA_MAX_PARAMS)
        params = CARLA_MAX_PARAMS;

    bfree(priv->paramDetails);
    priv->paramCount = params;
    priv->paramDetails = bzalloc(sizeof(struct carla_param_data) * params);

    char pname[PARAM_NAME_SIZE] = PARAM_NAME_INIT;

    for (uint32_t i=0; i < params; ++i)
    {
        const NativeParameter *const info = priv->descriptor->get_parameter_info(priv->handle, i);

        if ((info->hints & NATIVE_PARAMETER_IS_ENABLED) == 0)
            continue;
        if (info->hints & NATIVE_PARAMETER_IS_OUTPUT)
            continue;

        param_index_to_name(i, pname);
        priv->paramDetails[i].hints = info->hints;
        priv->paramDetails[i].min = info->ranges.min;
        priv->paramDetails[i].max = info->ranges.max;

        obs_property_t *prop;

        if (info->hints & NATIVE_PARAMETER_IS_BOOLEAN)
        {
            prop = obs_properties_add_bool(props, pname, info->name);

            obs_data_set_default_bool(settings, pname, info->ranges.def == info->ranges.max);

            if (reset)
                obs_data_set_bool(settings, pname, info->ranges.def == info->ranges.max);
        }
        else if (info->hints & NATIVE_PARAMETER_IS_INTEGER)
        {
            prop = obs_properties_add_int_slider(props, pname, info->name,
                                                 info->ranges.min, info->ranges.max, info->ranges.step);

            obs_data_set_default_int(settings, pname, info->ranges.def);

            if (info->unit && *info->unit)
                obs_property_int_set_suffix(prop, info->unit);

            if (reset)
                obs_data_set_int(settings, pname, info->ranges.def);
        }
        else
        {
            prop = obs_properties_add_float_slider(props, pname, info->name,
                                                   info->ranges.min, info->ranges.max, info->ranges.step);

            obs_data_set_default_double(settings, pname, info->ranges.def);

            if (info->unit && *info->unit)
                obs_property_float_set_suffix(prop, info->unit);

            if (reset)
                obs_data_set_double(settings, pname, info->ranges.def);
        }

        obs_property_set_modified_callback2(prop, carla_priv_param_changed, priv);
    }

    obs_data_release(settings);
}

// --------------------------------------------------------------------------------------------------------------------

static bool carla_post_load_callback(struct carla_priv *priv, obs_properties_t *props)
{
    obs_source_t *source = priv->source;
    obs_data_t *settings = obs_source_get_settings(source);
    remove_all_props(props, settings);
    carla_priv_readd_properties(priv, props, true);
    obs_data_release(settings);
    return true;
}

bool carla_priv_load_file_callback(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(property);

    struct carla_priv *priv = data;

    const char *filename = carla_qt_file_dialog(false, false, obs_module_text("Load File"), NULL);

    if (filename == NULL)
        return false;

    if (carla_get_current_plugin_count(priv->internalHostHandle) != 0)
        carla_replace_plugin(priv->internalHostHandle, 0);

    if (carla_load_file(priv->internalHostHandle, filename))
        return carla_post_load_callback(priv, props);

    return false;
}

bool carla_priv_select_plugin_callback(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(property);

    struct carla_priv *priv = data;

    const PluginListDialogResults *plugin = carla_frontend_createAndExecPluginListDialog(carla_qt_get_main_window());

    if (plugin == NULL)
        return false;

    if (carla_get_current_plugin_count(priv->internalHostHandle) != 0)
        carla_replace_plugin(priv->internalHostHandle, 0);

    if (carla_add_plugin(priv->internalHostHandle,
                         plugin->build, plugin->type,
                         plugin->filename, plugin->name, plugin->label, plugin->uniqueId,
                         NULL, PLUGIN_OPTIONS_NULL))
        return carla_post_load_callback(priv, props);

    return false;
}

bool carla_priv_show_gui_callback(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct carla_priv *priv = data;

    char winIdStr[24];
    snprintf(winIdStr, sizeof(winIdStr), "%llx", (ulonglong)carla_qt_get_main_window_id());
    carla_set_engine_option(priv->internalHostHandle, ENGINE_OPTION_FRONTEND_WIN_ID, 0, winIdStr);

    const double scaleFactor = carla_qt_get_scale_factor();
    carla_set_engine_option(priv->internalHostHandle, ENGINE_OPTION_FRONTEND_UI_SCALE, scaleFactor*1000, NULL);

    carla_show_custom_ui(priv->internalHostHandle, 0, true);

    return false;
}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_free(void *data)
{
    free(data);
}

// --------------------------------------------------------------------------------------------------------------------