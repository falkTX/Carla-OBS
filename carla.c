/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// IDE helpers, must match cmake config
#define CARLA_PLUGIN_BUILD 1
#define HAVE_X11 1
#define REAL_BUILD 1
#define STATIC_PLUGIN_TARGET 1

#include <obs-module.h>
#include <util/platform.h>
#include "qtutils.h"

#include "CarlaNativePlugin.h"
#include "CarlaFrontend.h"

// property names
#define PROP_SELECT_PLUGIN "select-plugin"
#define PROP_SHOW_GUI "show-gui"
#define PROP_RELOAD "reload"

// generates a warning if this is defined as anything else
#define CARLA_API

// maximum buffer used, could be lower
#define MAX_AUDIO_BUFFER_SIZE 512

#define TRACE_CALL printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %s %d\n", __FUNCTION__, __LINE__);

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
// helper methods

struct carla_param_data {
    uint32_t hints;
    float min, max;
};

struct carla_main_thread_param_change {
    const NativePluginDescriptor* descriptor;
    NativePluginHandle handle;
    uint32_t index;
    float value;
};

static void carla_main_thread_param_change(void *data)
{
    struct carla_main_thread_param_change *carla = data;
    carla->descriptor->ui_set_parameter_value(carla->handle, carla->index, carla->value);
    free(data);
}

// --------------------------------------------------------------------------------------------------------------------
// carla + obs integration methods

struct carla_data {
    obs_source_t *source;

    // carla host
    const NativePluginDescriptor* descriptor;
    NativePluginHandle handle;
    NativeHostDescriptor host;
    NativeTimeInfo timeInfo;
    CarlaHostHandle internalHostHandle;

    // internal buffering
    uint32_t bufferPos;
    uint32_t bufferSize;
    float *abuffer1, *abuffer2;

    // current OBS config
    size_t channels;
    uint32_t sampleRate;

    // parameter handling
    struct carla_param_data* paramDetails;
};

static const char *carla_obs_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("Carla Plugin Host");
}

static void *carla_obs_alloc(void)
{
    struct carla_data *carla = bzalloc(sizeof(*carla));
    if (carla == NULL)
        return NULL;

    carla->abuffer1 = bzalloc(sizeof(float) * MAX_AUDIO_BUFFER_SIZE);
    if (carla->abuffer1 == NULL)
        goto fail;

    carla->abuffer2 = bzalloc(sizeof(float) * MAX_AUDIO_BUFFER_SIZE);
    if (carla->abuffer2 == NULL)
        goto failbuf2;

    return carla;

failbuf2:
    bfree(carla->abuffer1);

fail:
    bfree(carla);
    return NULL;
}

static void carla_obs_idle_callback(void *data, float unused)
{
    UNUSED_PARAMETER(unused);
    struct carla_data *carla = data;
    carla->descriptor->ui_idle(carla->handle);
}

static void *carla_obs_create(obs_data_t *settings, obs_source_t *source)
{
    TRACE_CALL
    const NativePluginDescriptor* descriptor = carla_get_native_rack_plugin();
    if (descriptor == NULL)
        return NULL;

    struct carla_data *carla = carla_obs_alloc();
    if (carla == NULL)
        return NULL;

    carla->source = source;

    carla->bufferPos = 0;
    carla->bufferSize = MAX_AUDIO_BUFFER_SIZE;

    const audio_t *audio = obs_get_audio();
    carla->channels = audio_output_get_channels(audio);
    carla->sampleRate = audio_output_get_sample_rate(audio);

    if (carla->channels == 0 || carla->sampleRate == 0)
        goto fail;

    carla->descriptor = descriptor;

    {
        NativeHostDescriptor host = {
            .handle = carla,
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
        goto failhost;

    descriptor->dispatcher(carla->handle, NATIVE_PLUGIN_OPCODE_HOST_USES_EMBED, 0, 0, NULL, 0.f);
    descriptor->activate(carla->handle);

    obs_add_tick_callback(carla_obs_idle_callback, carla);

    return carla;

failhost:
    descriptor->cleanup(carla->handle);

fail:
    bfree(carla->abuffer2);
    bfree(carla->abuffer1);
    bfree(carla);
    return NULL;
}

static void carla_obs_destroy(void *data)
{
    TRACE_CALL
    struct carla_data *carla = data;
    obs_remove_tick_callback(carla_obs_idle_callback, carla);
    carla_host_handle_free(carla->internalHostHandle);
    carla->descriptor->deactivate(carla->handle);
    carla->descriptor->cleanup(carla->handle);
    bfree(carla->abuffer2);
    bfree(carla->abuffer1);
    bfree(carla->paramDetails);
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
            carla->timeInfo.usecs = os_gettime_ns() * 1000;
            carla->descriptor->process(carla->handle, carlabuffers, carlabuffers, bufferSize, NULL, 0);
        }

        obsbuffers[1][i] = carlabuffers[1][j];
        obsbuffers[0][i] = carlabuffers[0][j];
    }

    carla->bufferPos = bufferPos;

    return audio;
}

static bool carla_obs_plugin_add_callback(obs_properties_t *props, obs_property_t *property, void *data)
{
    TRACE_CALL
    UNUSED_PARAMETER(property);
    struct carla_data *carla = data;

    const PluginListDialogResults *plugin = carla_frontend_createAndExecPluginListDialog(carla_obs_get_main_window());

    if (plugin == NULL)
        return false;

    if (carla_get_current_plugin_count(carla->internalHostHandle) != 0)
        carla_replace_plugin(carla->internalHostHandle, 0);

    if (carla_add_plugin(carla->internalHostHandle,
                         plugin->build, plugin->type,
                         plugin->filename, plugin->name, plugin->label, plugin->uniqueId,
                         NULL, PLUGIN_OPTIONS_NULL))
    {
        obs_source_t *source = carla->source;
        obs_data_t *settings = obs_source_get_settings(source);
        obs_data_set_int(settings, PROP_RELOAD, 1);
        obs_properties_apply_settings(props, settings);
        obs_data_release(settings);
        return true;
    }

    TRACE_CALL
    return false;
}

static bool carla_obs_show_gui_callback(obs_properties_t *props, obs_property_t *property, void *data)
{
    TRACE_CALL
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct carla_data *carla = data;

    // TODO open plugin list dialog
    char winIdStr[24];
    snprintf(winIdStr, sizeof(winIdStr), "%lx", (ulong)carla_obs_get_main_window_id());
    carla_set_engine_option(carla->internalHostHandle, ENGINE_OPTION_FRONTEND_WIN_ID, 0, winIdStr);
    // carla_set_engine_option(handle, ENGINE_OPTION_FRONTEND_UI_SCALE, scaleFactor*1000, nullptr);

    carla_show_custom_ui(carla->internalHostHandle, 0, true);

    TRACE_CALL
    return false;
}

static bool carla_obs_param_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings);

static void carla_obs_readd_properties(struct carla_data *carla, obs_properties_t *props, obs_data_t *settings)
{
    // show/hide GUI button
    const bool hasGUI = carla_get_current_plugin_count(carla->internalHostHandle) != 0 &&
                        carla_get_plugin_info(carla->internalHostHandle, 0)->hints & PLUGIN_HAS_CUSTOM_UI;
    if (hasGUI)
    {
        // obs_property_t *gui = obs_properties_get(props, PROP_SHOW_GUI);
        obs_properties_add_button(props, PROP_SHOW_GUI, obs_module_text("Show custom GUI"), carla_obs_show_gui_callback);
        // obs_property_set_enabled(gui, hasGUI);
        // obs_property_set_visible(gui, hasGUI);
    }

    // reload handling
    {
        obs_property_t *reload = obs_properties_add_int_slider(props, PROP_RELOAD, obs_module_text("Needs Reload"), 0, 1, 1);
        obs_data_set_default_int(settings, PROP_RELOAD, 0);
        obs_property_set_modified_callback2(reload, carla_obs_param_changed, carla);
        obs_property_set_visible(reload, false);
    }

    const uint32_t params = carla->descriptor->get_parameter_count(carla->handle);
    char pname[] = {'p','0','0','0','\0'};

    bfree(carla->paramDetails);
    carla->paramDetails = bzalloc(sizeof(struct carla_param_data) * params);

    for (uint32_t i=0; i < params; ++i)
    {
        const NativeParameter *const info = carla->descriptor->get_parameter_info(carla->handle, i);

        if ((info->hints & NATIVE_PARAMETER_IS_ENABLED) == 0)
            continue;
        if (info->hints & NATIVE_PARAMETER_IS_OUTPUT)
            break;

        carla->paramDetails[i].hints = info->hints;
        carla->paramDetails[i].min = info->ranges.min;
        carla->paramDetails[i].max = info->ranges.max;

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

        obs_property_set_modified_callback2(prop, carla_obs_param_changed, carla);
    }
}

static bool carla_obs_param_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    struct carla_data *carla = data;

    const char *const pname = obs_property_name(property);
    if (pname == NULL)
    {
        printf("param changed %s | FAIL\n", pname);
        return false;
    }

    printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> %s %d | %s\n", __FUNCTION__, __LINE__, pname);

    if (!strcmp(pname, PROP_RELOAD))
    {
        // dont reload again
        if (obs_data_get_int(settings, pname) == 0)
            return false;
        obs_data_set_int(settings, PROP_RELOAD, 0);

        printf("carla_obs_param_changed needsReload\n");

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

        carla_obs_readd_properties(carla, props, settings);
        return true;
    }

    printf("carla_obs_param_changed has props:");
    for (obs_property_t *prop = obs_properties_first(props); prop != NULL; obs_property_next(&prop))
    {
         printf(" %s,", obs_property_name(prop));
    }
    printf("\n");

    printf("carla_obs_param_changed has settings:");
    for (obs_data_item_t *item = obs_data_first(settings); item != NULL; obs_data_item_next(&item))
    {
         printf(" %s,", obs_data_item_get_name(item));
    }
    printf("\n");

    const char* pname2 = pname + 1;
    while (*pname2 == '0')
        ++pname2;

    const int pindex = atoi(pname2);

    const float min = carla->paramDetails[pindex].min;
    const float max = carla->paramDetails[pindex].max;

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

    printf("param changed %d:%s %f\n", pindex, pname, value);
    carla->descriptor->set_parameter_value(carla->handle, pindex, value);

    // UI param change notification needs to happen on main thread
    struct carla_main_thread_param_change mchange = {
        .descriptor = carla->descriptor,
        .handle = carla->handle,
        .index = pindex,
        .value = value
    };
    struct carla_main_thread_param_change *mchangeptr = malloc(sizeof(mchange));
    *mchangeptr = mchange;
    carla_obs_callback_on_main_thread(carla_main_thread_param_change, mchangeptr);

    return false;
}

static obs_properties_t *carla_obs_get_properties(void *data)
{
    TRACE_CALL
    struct carla_data *carla = data;

    obs_properties_t *props = obs_properties_create();
    obs_data_t *settings = obs_source_get_settings(carla->source);

    obs_properties_add_button(props, PROP_SELECT_PLUGIN, obs_module_text("Select plugin..."), carla_obs_plugin_add_callback);

    carla_obs_readd_properties(carla, props, settings);

    obs_data_release(settings);

    TRACE_CALL
    return props;
}

static void carla_obs_save(void *data, obs_data_t *settings)
{
    TRACE_CALL
    struct carla_data *carla = data;

    char *state = carla->descriptor->get_state(carla->handle);
    if (state)
    {
        obs_data_set_string(settings, "state", state);
        free(state);
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
        carla->descriptor->set_state(carla->handle, state);
    }

    TRACE_CALL
}

// --------------------------------------------------------------------------------------------------------------------
// carla host methods

static uint32_t host_get_buffer_size(NativeHostHandle handle)
{
    const struct carla_data *carla = handle;
    return carla->bufferSize;
}

static double host_get_sample_rate(NativeHostHandle handle)
{
    const struct carla_data *carla = handle;
    return carla->sampleRate;
}

static bool host_is_offline(NativeHostHandle handle)
{
    UNUSED_PARAMETER(handle);
    return false;
}

static const NativeTimeInfo* host_get_time_info(NativeHostHandle handle)
{
    const struct carla_data *carla = handle;
    return &carla->timeInfo;
}

static bool host_write_midi_event(NativeHostHandle handle, const NativeMidiEvent *event)
{
    UNUSED_PARAMETER(handle);
    UNUSED_PARAMETER(event);
    return false;
}

static void host_ui_parameter_changed(NativeHostHandle handle, uint32_t index, float value)
{
    struct carla_data *carla = handle;

    const uint32_t hints = carla->paramDetails[index].hints;
    if ((hints & NATIVE_PARAMETER_IS_ENABLED) == 0)
        return;

    char pname[] = {'p','0','0','0','\0'};
    pname[1] = '0' + ((index / 100) % 10);
    pname[2] = '0' + ((index / 10) % 10);
    pname[3] = '0' + (index % 10);
    printf("host_ui_parameter_changed %d:%s to %f\n", index, pname, value);

    // FIXME this doesnt really work

    obs_source_t *source = carla->source;
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
}

static const char* host_ui_open_file(NativeHostHandle handle, bool isDir, const char *title, const char *filter)
{
    UNUSED_PARAMETER(handle);
    return carla_obs_file_dialog(false, isDir, title, filter);
}

static const char* host_ui_save_file(NativeHostHandle handle, bool isDir, const char *title, const char *filter)
{
    UNUSED_PARAMETER(handle);
    return carla_obs_file_dialog(true, isDir, title, filter);
}

static intptr_t host_dispatcher(NativeHostHandle handle, NativeHostDispatcherOpcode opcode,
                                int32_t index, intptr_t value, void *ptr, float opt)
{
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
    filter.get_properties = carla_obs_get_properties;
    filter.save = carla_obs_save;
    filter.load = carla_obs_load;

    obs_register_source(&filter);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------
