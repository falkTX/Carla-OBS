/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-wrapper.h"
#include "qtutils.h"
#include <util/platform.h>

// TESTING audio generator mode
#include <threads.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

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
    free(data);
}

// --------------------------------------------------------------------------------------------------------------------
// private data methods

#define CARLA_AUDIO_GEN_BUFFER_SIZE 512

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
    struct carla_param_data* paramDetails;

    // TESTING audio generator mode
    thrd_t thread;
    volatile bool active;
    volatile bool runningThread;
};

static int carla_audio_gen_thread(void *data)
{
    struct carla_priv *priv = data;

    float silence1[CARLA_AUDIO_GEN_BUFFER_SIZE] = {0.f};
    float silence2[CARLA_AUDIO_GEN_BUFFER_SIZE] = {0.f};
    float* silences[2] = {silence1,silence2};

    float buf1[CARLA_AUDIO_GEN_BUFFER_SIZE] = {0.f};
    float buf2[CARLA_AUDIO_GEN_BUFFER_SIZE] = {0.f};
    float* bufs[2] = {buf1,buf2};

    struct obs_source_audio out = {};
    out.data[0] = (uint8_t *)buf1;
    out.data[1] = (uint8_t *)buf2;
    for (int i=2; i<MAX_AV_PLANES; ++i)
        out.data[i] = NULL;
    out.frames = CARLA_AUDIO_GEN_BUFFER_SIZE;
    out.speakers = SPEAKERS_STEREO;
    out.format = AUDIO_FORMAT_FLOAT_PLANAR;
    out.samples_per_sec = priv->sampleRate;

    const uint64_t slice = CARLA_AUDIO_GEN_BUFFER_SIZE * 1000000000ULL / priv->sampleRate;

    uint32_t xruns = 0;
    uint64_t now, prev, diff;
    prev = now = os_gettime_ns();

    while (priv->runningThread)
    {
        prev = now = os_gettime_ns();

        if (priv->active)
        {
            out.timestamp = now;
            priv->timeInfo.usecs = now / 1000;
            priv->descriptor->process(priv->handle, silences, bufs, CARLA_AUDIO_GEN_BUFFER_SIZE, NULL, 0);
            obs_source_output_audio(priv->source, &out);
            now = os_gettime_ns();
        }

        // time went backwards!
        if (now < prev) {
            printf("_______________________________________________________________________________ backwards time\n");
            if (++xruns == 1000)
                break;
            continue;
        }

        diff = now - prev;

        // xrun!
        if (slice <= diff) {
            printf("______________________________________________________________________ xrun | %lu %lu | %lu %lu\n",
                   slice, diff, prev, now);
            if (++xruns == 1000)
                break;
            continue;
        }

#if 0
        // FIXME get this part to work
        struct timespec nowts;
        clock_gettime(CLOCK_REALTIME, &nowts);
        now = nowts.tv_sec * 1000000000ULL + nowts.tv_nsec;

        const uint64_t target = now + slice - diff;
        const struct timespec timeout = {
            target / 1000000000ULL,
            target % 1000000000ULL
        };
        thrd_sleep(&timeout, NULL);
#else
        usleep((slice - diff) / 1000);
#endif
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

    const uint32_t hints = priv->paramDetails[index].hints;
    if ((hints & NATIVE_PARAMETER_IS_ENABLED) == 0)
        return;

    char pname[] = {'p','0','0','0','\0'};
    pname[1] = '0' + ((index / 100) % 10);
    pname[2] = '0' + ((index / 10) % 10);
    pname[3] = '0' + (index % 10);
    printf("host_ui_parameter_changed %d:%s to %f\n", index, pname, value);

    // FIXME this doesnt really work

    obs_source_t *source = priv->source;
    obs_data_t *settings = obs_source_get_settings(source);

    if (hints & NATIVE_PARAMETER_IS_BOOLEAN)
        obs_data_set_bool(settings, pname, value > 0.5f ? 1.f : 0.f);
    else if (hints & NATIVE_PARAMETER_IS_INTEGER)
        obs_data_set_int(settings, pname, value);
    else
        obs_data_set_double(settings, pname, value);

    obs_data_release(settings);
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

struct carla_priv *carla_priv_create(obs_source_t *source, uint32_t bufferSize, uint32_t sampleRate)
{
    const NativePluginDescriptor* descriptor = carla_get_native_rack_plugin();
    if (descriptor == NULL)
        return NULL;

    struct carla_priv *priv = bzalloc(sizeof(struct carla_priv));
    if (priv == NULL)
        return NULL;

    priv->source = source;
    priv->bufferSize = bufferSize;
    priv->sampleRate = sampleRate;
    priv->descriptor = descriptor;

    if (bufferSize == 0)
    {
        // TESTING audio generator mode
        priv->bufferSize = CARLA_AUDIO_GEN_BUFFER_SIZE;
        priv->runningThread = true;
    }

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
            .usecs = os_gettime_ns() * 1000,
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

    // TODO obs_source_active
    priv->active = true;
    priv->descriptor->activate(priv->handle);

    // TESTING audio generator mode
    if (priv->runningThread)
        thrd_create(&priv->thread, carla_audio_gen_thread, priv);

    return priv;

fail2:
    descriptor->cleanup(&priv->host);

fail1:
    bfree(priv);
    return NULL;
}

void carla_priv_destroy(struct carla_priv *priv)
{
    if (priv->runningThread)
    {
        priv->runningThread = false;
        thrd_join(priv->thread, NULL);
    }

    if (priv->active)
        priv->descriptor->deactivate(priv->handle);

    carla_host_handle_free(priv->internalHostHandle);
    priv->descriptor->cleanup(priv->handle);
    bfree(priv->paramDetails);
    bfree(priv);
}

// --------------------------------------------------------------------------------------------------------------------

// void carla_priv_activate(struct carla_priv *priv)
// {
//     priv->descriptor->activate(priv->handle);
//     priv->active = true;
// }
//
// void carla_priv_deactivate(struct carla_priv *priv)
// {
//     if (priv->runningThread)
//     {
//         priv->runningThread = false;
//         thrd_join(priv->thread, NULL);
//     }
//
//     priv->active = false;
//     priv->descriptor->deactivate(priv->handle);
// }

void carla_priv_process_audio(struct carla_priv *priv, float *buffers[2], uint32_t frames)
{
    priv->timeInfo.usecs = os_gettime_ns() * 1000;
    priv->descriptor->process(priv->handle, buffers, buffers, frames, NULL, 0);
}

void carla_priv_idle(struct carla_priv *priv)
{
    priv->descriptor->ui_idle(priv->handle);
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

static bool carla_priv_param_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    struct carla_priv *priv = data;

    const char *const pname = obs_property_name(property);
    if (pname == NULL)
    {
        printf("param changed | FAIL\n");
        return false;
    }

    if (!strcmp(pname, PROP_RELOAD))
    {
        printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %s %d | %s\n", __FUNCTION__, __LINE__, pname);

        printf("carla_priv_param_changed has props:");
        for (obs_property_t *prop = obs_properties_first(props); prop != NULL; obs_property_next(&prop))
        {
            printf(" %s,", obs_property_name(prop));
        }
        printf("\n");

        printf("carla_priv_param_changed has settings:");
        for (obs_data_item_t *item = obs_data_first(settings); item != NULL; obs_data_item_next(&item))
        {
            printf(" %s,", obs_data_item_get_name(item));
        }
        printf("\n");

        // dont reload again
        if (obs_data_get_int(settings, pname) == 0)
            return false;
        obs_data_set_int(settings, PROP_RELOAD, 0);

        printf("carla_priv_param_changed needsReload\n");

        char pname[] = {'p','0','0','0','\0'};

        obs_data_unset_default_value(settings, PROP_RELOAD);
        obs_data_unset_default_value(settings, PROP_SHOW_GUI);

        obs_properties_remove_by_name(props, PROP_RELOAD);
        obs_properties_remove_by_name(props, PROP_SHOW_GUI);

        for (uint32_t i=0; i < 120; ++i)
        {
            pname[1] = '0' + ((i / 100) % 10);
            pname[2] = '0' + ((i / 10) % 10);
            pname[3] = '0' + (i % 10);

            obs_data_unset_default_value(settings, pname);
            obs_properties_remove_by_name(props, pname);
        }

        carla_priv_readd_properties(priv, props);
        return true;
    }
    const char* pname2 = pname + 1;
    while (*pname2 == '0')
        ++pname2;

    const int pindex = atoi(pname2);

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
    struct carla_main_thread_param_change *mchangeptr = malloc(sizeof(mchange));
    *mchangeptr = mchange;
    carla_qt_callback_on_main_thread(carla_main_thread_param_change, mchangeptr);

    return false;
}

void carla_priv_readd_properties(struct carla_priv *priv, obs_properties_t *props)
{
    obs_data_t *settings = obs_source_get_settings(priv->source);

    // show/hide GUI button
    const bool hasGUI = carla_get_current_plugin_count(priv->internalHostHandle) != 0 &&
                        carla_get_plugin_info(priv->internalHostHandle, 0)->hints & PLUGIN_HAS_CUSTOM_UI;
    if (hasGUI)
    {
        // obs_property_t *gui = obs_properties_get(props, PROP_SHOW_GUI);
        obs_properties_add_button2(props, PROP_SHOW_GUI, obs_module_text("Show custom GUI"), carla_priv_show_gui_callback, priv);
        // obs_property_set_enabled(gui, hasGUI);
        // obs_property_set_visible(gui, hasGUI);
    }

    // reload handling
    {
        obs_property_t *reload = obs_properties_add_int_slider(props, PROP_RELOAD, obs_module_text("Needs Reload"), 0, 1, 1);
        obs_data_set_default_int(settings, PROP_RELOAD, 0);
        obs_property_set_modified_callback2(reload, carla_priv_param_changed, priv);
        obs_property_set_visible(reload, false);
    }

    const uint32_t params = priv->descriptor->get_parameter_count(priv->handle);
    char pname[] = {'p','0','0','0','\0'};

    bfree(priv->paramDetails);
    priv->paramDetails = bzalloc(sizeof(struct carla_param_data) * params);

    for (uint32_t i=0; i < params; ++i)
    {
        const NativeParameter *const info = priv->descriptor->get_parameter_info(priv->handle, i);

        if ((info->hints & NATIVE_PARAMETER_IS_ENABLED) == 0)
            continue;
        if (info->hints & NATIVE_PARAMETER_IS_OUTPUT)
            break;

        priv->paramDetails[i].hints = info->hints;
        priv->paramDetails[i].min = info->ranges.min;
        priv->paramDetails[i].max = info->ranges.max;

        pname[1] = '0' + ((i / 100) % 10);
        pname[2] = '0' + ((i / 10) % 10);
        pname[3] = '0' + (i % 10);

        obs_property_t *prop;

        if (info->hints & NATIVE_PARAMETER_IS_BOOLEAN)
        {
            prop = obs_properties_add_bool(props, pname, info->name);

            obs_data_set_default_bool(settings, pname, info->ranges.def == info->ranges.max);
        }
        else if (info->hints & PARAMETER_IS_INTEGER)
        {
            prop = obs_properties_add_int_slider(props, pname, info->name, info->ranges.min, info->ranges.max, 1);

            obs_data_set_default_int(settings, pname, info->ranges.def);

            if (info->unit && *info->unit)
                obs_property_int_set_suffix(prop, info->unit);
        }
        else
        {
            prop = obs_properties_add_float_slider(props, pname, info->name,
                                                   info->ranges.min, info->ranges.max, info->ranges.step);

            obs_data_set_default_double(settings, pname, info->ranges.def);

            if (info->unit && *info->unit)
                obs_property_float_set_suffix(prop, info->unit);
        }

        obs_property_set_modified_callback2(prop, carla_priv_param_changed, priv);
    }

    obs_data_release(settings);
}

// --------------------------------------------------------------------------------------------------------------------

bool carla_priv_select_plugin_callback(obs_properties_t *props, obs_property_t *property, void *data)
{
    TRACE_CALL
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
    {
        obs_source_t *source = source;
        obs_data_t *settings = obs_source_get_settings(source);
        obs_data_set_int(settings, PROP_RELOAD, 1);
        obs_properties_apply_settings(props, settings);
        obs_data_release(settings);
        return true;
    }

    TRACE_CALL
    return false;
}

bool carla_priv_show_gui_callback(obs_properties_t *props, obs_property_t *property, void *data)
{
    TRACE_CALL
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct carla_priv *priv = data;

    // TODO open plugin list dialog
    char winIdStr[24];
    snprintf(winIdStr, sizeof(winIdStr), "%lx", (ulong)carla_qt_get_main_window_id());
    carla_set_engine_option(priv->internalHostHandle, ENGINE_OPTION_FRONTEND_WIN_ID, 0, winIdStr);
    // carla_set_engine_option(priv->internalHostHandle, ENGINE_OPTION_FRONTEND_UI_SCALE, scaleFactor*1000, nullptr);

    carla_show_custom_ui(priv->internalHostHandle, 0, true);

    TRACE_CALL
    return false;
}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_free(void *data)
{
    free(data);
}

// --------------------------------------------------------------------------------------------------------------------
