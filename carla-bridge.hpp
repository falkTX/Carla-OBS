/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "CarlaBackend.h"
#include "CarlaBridgeUtils.hpp"

#include "water/threads/ChildProcess.h"

CARLA_BACKEND_USE_NAMESPACE

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
    CarlaScopedPointer<water::ChildProcess> process;
};

// --------------------------------------------------------------------------------------------------------------------
