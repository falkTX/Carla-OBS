/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-wrapper.h"
#include "qtutils.h"
#include <util/platform.h>

#include "CarlaBridgeUtils.hpp"
#include "CarlaFrontend.h"

// generates a warning if this is defined as anything else
#define CARLA_API

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

    struct {
        BridgeAudioPool          audiopool; // fShmAudioPool
        BridgeRtClientControl    rtClientCtrl; // fShmRtClientControl
        BridgeNonRtClientControl nonRtClientCtrl; // fShmNonRtClientControl
        BridgeNonRtServerControl nonRtServerCtrl; // fShmNonRtServerControl

        // reset memory
        void init(uint32_t bufferSize, double sampleRate)
        {
            rtClientCtrl.data->procFlags = 0;
            carla_zeroStruct(rtClientCtrl.data->timeInfo);
            carla_zeroBytes(rtClientCtrl.data->midiOut, kBridgeRtClientDataMidiOutSize);

            rtClientCtrl.clearData();
            nonRtClientCtrl.clearData();
            nonRtServerCtrl.clearData();

            nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientVersion);
            nonRtClientCtrl.writeUInt(CARLA_PLUGIN_BRIDGE_API_VERSION_CURRENT);

            nonRtClientCtrl.writeUInt(static_cast<uint32_t>(sizeof(BridgeRtClientData)));
            nonRtClientCtrl.writeUInt(static_cast<uint32_t>(sizeof(BridgeNonRtClientData)));
            nonRtClientCtrl.writeUInt(static_cast<uint32_t>(sizeof(BridgeNonRtServerData)));

            nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientInitialSetup);
            nonRtClientCtrl.writeUInt(bufferSize);
            nonRtClientCtrl.writeDouble(sampleRate);

            nonRtClientCtrl.commitWrite();

            if (audiopool.dataSize != 0)
            {
                rtClientCtrl.writeOpcode(kPluginBridgeRtClientSetAudioPool);
                rtClientCtrl.writeULong(static_cast<uint64_t>(audiopool.dataSize));
                rtClientCtrl.commitWrite();
            }
            else
            {
                // testing dummy message
                rtClientCtrl.writeOpcode(kPluginBridgeRtClientNull);
                rtClientCtrl.commitWrite();
            }
        }

        void cleanup()
        {
            nonRtServerCtrl.clear();
            nonRtClientCtrl.clear();
            rtClientCtrl.clear();
            audiopool.clear();
        }
    } bridge;
};

// --------------------------------------------------------------------------------------------------------------------
// carla + obs integration methods

struct carla_priv *carla_priv_create(obs_source_t *source, enum buffer_size_mode bufsize, uint32_t srate, bool filter)
{
    struct carla_priv *priv = new struct carla_priv;
    if (priv == NULL)
        return NULL;

    priv->source = source;
    priv->bufferSize = bufsize_mode_to_frames(bufsize);
    priv->sampleRate = srate;

    priv->bridge.init(priv->bufferSize, srate);

    return priv;
}

void carla_priv_destroy(struct carla_priv *priv)
{
    priv->bridge.cleanup();
    bfree(priv);
}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_activate(struct carla_priv *priv)
{
}

void carla_priv_deactivate(struct carla_priv *priv)
{
}

void carla_priv_process_audio(struct carla_priv *priv, float *buffers[2], uint32_t frames)
{
}

void carla_priv_idle(struct carla_priv *priv)
{
}

char *carla_priv_get_state(struct carla_priv *priv)
{
    return NULL;
}

void carla_priv_set_state(struct carla_priv *priv, const char *state)
{
}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_set_buffer_size(struct carla_priv *priv, enum buffer_size_mode bufsize)
{
}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_readd_properties(struct carla_priv *priv, obs_properties_t *props, bool reset)
{
}

// --------------------------------------------------------------------------------------------------------------------

bool carla_priv_load_file_callback(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(property);

    struct carla_priv *priv = static_cast<struct carla_priv*>(data);

    const char *filename = carla_qt_file_dialog(false, false, obs_module_text("Load File"), NULL);

    if (filename == NULL)
        return false;
    return false;
}

bool carla_priv_select_plugin_callback(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(property);

    struct carla_priv *priv = static_cast<struct carla_priv*>(data);

    const PluginListDialogResults *plugin = carla_frontend_createAndExecPluginListDialog(carla_qt_get_main_window());

    if (plugin == NULL)
        return false;

    return false;
}

bool carla_priv_show_gui_callback(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct carla_priv *priv = static_cast<struct carla_priv*>(data);

    return false;
}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_free(void *data)
{
    free(data);
}

// --------------------------------------------------------------------------------------------------------------------
