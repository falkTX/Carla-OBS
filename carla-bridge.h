/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "CarlaBackend.h"
#include "CarlaBridgeUtils.hpp"
#include "CarlaJuceUtils.hpp"
#include "CarlaThread.hpp"

#include "water/threads/ChildProcess.h"

CARLA_BACKEND_USE_NAMESPACE

// --------------------------------------------------------------------------------------------------------------------

class CarlaPluginBridgeThread : public CarlaThread
{
public:
    CarlaPluginBridgeThread() noexcept;
    void setData(const PluginType type,
                 const char* const binaryArchName,
                 const char* const bridgeBinary,
                 const char* const label,
                 const char* const filename,
                 const int64_t uniqueId,
                 const char* const shmIds) noexcept;

protected:
    void run();

private:
    PluginType fPluginType;
    int64_t fPluginUniqueId;
    water::String fBinaryArchName;
    water::String fBridgeBinary;
    water::String fPluginFilename;
    water::String fPluginLabel;
    water::String fShmIds;
#ifndef CARLA_OS_WIN
    water::String fWinePrefix;
#endif

    CarlaScopedPointer<water::ChildProcess> fProcess;

    CARLA_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CarlaPluginBridgeThread)
};

// --------------------------------------------------------------------------------------------------------------------

struct carla_param_data {
    uint32_t hints = 0;
    float value = 0.f;
    float def = 0.f;
    float min = 0.f;
    float max = 1.f;
    float step = 0.01f;
    CarlaString name;
    CarlaString symbol;
    CarlaString unit;
};

struct carla_bridge {
    BridgeAudioPool          audiopool; // fShmAudioPool
    BridgeRtClientControl    rtClientCtrl; // fShmRtClientControl
    BridgeNonRtClientControl nonRtClientCtrl; // fShmNonRtClientControl
    BridgeNonRtServerControl nonRtServerCtrl; // fShmNonRtServerControl

    CarlaPluginBridgeThread thread;

    // init sem/shm
    bool init(uint32_t bufferSize, double sampleRate);
    void cleanup();
};

// --------------------------------------------------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

void carla_bridge_destroy(struct carla_bridge *bridge);

void carla_bridge_activate(struct carla_bridge *bridge);
void carla_bridge_deactivate(struct carla_bridge *bridge);

#ifdef __cplusplus
}
#endif

// --------------------------------------------------------------------------------------------------------------------
