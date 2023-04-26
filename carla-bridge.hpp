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

    CarlaScopedPointer<water::ChildProcess> fProcess;

    CARLA_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CarlaPluginBridgeThread)
};

// --------------------------------------------------------------------------------------------------------------------

struct carla_bridge {
    BridgeAudioPool          audiopool; // fShmAudioPool
    BridgeRtClientControl    rtClientCtrl; // fShmRtClientControl
    BridgeNonRtClientControl nonRtClientCtrl; // fShmNonRtClientControl
    BridgeNonRtServerControl nonRtServerCtrl; // fShmNonRtServerControl

    bool timedOut = false;

    bool init(uint32_t bufferSize, double sampleRate);
    void cleanup();

    bool start(PluginType type,
               const char* binaryArchName,
               const char* bridgeBinary,
               const char* label,
               const char* filename,
               int64_t uniqueId);
    bool isRunning() const;

    // waits on RT client, making sure it is still active
    void wait(const char* action, uint msecs);

    void activate();
    void deactivate();

private:
    char shmIdsStr[6*4+1] = {};
    CarlaPluginBridgeThread thread;
};

// --------------------------------------------------------------------------------------------------------------------
