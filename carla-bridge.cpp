/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-bridge.hpp"

#include <obs-module.h>

#include "CarlaBackendUtils.hpp"

#include "water/files/File.h"
#include "water/misc/Time.h"

#include <ctime>

// --------------------------------------------------------------------------------------------------------------------

static void stopProcess(water::ChildProcess* const process)
{
    // we only get here if bridge crashed or thread asked to exit
    if (process->isRunning())
    {
        process->waitForProcessToFinish(2000);

        if (process->isRunning())
        {
            carla_stdout("CarlaPluginBridgeThread::run() - bridge refused to close, force kill now");
            process->kill();
        }
        else
        {
            carla_stdout("CarlaPluginBridgeThread::run() - bridge auto-closed successfully");
        }
    }
    else
    {
        // forced quit, may have crashed
        if (process->getExitCodeAndClearPID() != 0)
        {
            carla_stderr("CarlaPluginBridgeThread::run() - bridge crashed");

            CarlaString errorString("Plugin has crashed!\n"
                                    "Saving now will lose its current settings.\n"
                                    "Please remove this plugin, and not rely on it from this point.");
        }
    }
}

// --------------------------------------------------------------------------------------------------------------------

bool carla_bridge::init(uint32_t bufferSize, double sampleRate)
{
    std::srand(static_cast<uint>(std::time(nullptr)));

    if (! audiopool.initializeServer())
    {
        carla_stderr("Failed to initialize shared memory audio pool");
        return false;
    }

    audiopool.resize(bufferSize, MAX_AV_PLANES, MAX_AV_PLANES);

    if (! rtClientCtrl.initializeServer())
    {
        carla_stderr("Failed to initialize RT client control");
        goto fail1;
    }

    if (! nonRtClientCtrl.initializeServer())
    {
        carla_stderr("Failed to initialize Non-RT client control");
        goto fail2;
    }

    if (! nonRtServerCtrl.initializeServer())
    {
        carla_stderr("Failed to initialize Non-RT server control");
        goto fail3;
    }

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

    rtClientCtrl.writeOpcode(kPluginBridgeRtClientSetAudioPool);
    rtClientCtrl.writeULong(static_cast<uint64_t>(audiopool.dataSize));
    rtClientCtrl.commitWrite();

    rtClientCtrl.writeOpcode(kPluginBridgeRtClientSetBufferSize);
    rtClientCtrl.writeUInt(bufferSize);
    rtClientCtrl.commitWrite();

    carla_zeroStruct(shmIdsStr);
    std::strncpy(shmIdsStr+6*0, &audiopool.filename[audiopool.filename.length()-6], 6);
    std::strncpy(shmIdsStr+6*1, &rtClientCtrl.filename[rtClientCtrl.filename.length()-6], 6);
    std::strncpy(shmIdsStr+6*2, &nonRtClientCtrl.filename[nonRtClientCtrl.filename.length()-6], 6);
    std::strncpy(shmIdsStr+6*3, &nonRtServerCtrl.filename[nonRtServerCtrl.filename.length()-6], 6);

    timedOut = false;

    return true;

fail3:
    nonRtClientCtrl.clear();

fail2:
    rtClientCtrl.clear();

fail1:
    audiopool.clear();
    return false;
}

void carla_bridge::cleanup()
{
    if (process != nullptr)
    {
        if (process->isRunning())
        {
            nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientQuit);
            nonRtClientCtrl.commitWrite();

            rtClientCtrl.writeOpcode(kPluginBridgeRtClientQuit);
            rtClientCtrl.commitWrite();

            if (! timedOut)
                wait("stopping", 3000);
        }

        stopProcess(process);
        process = nullptr;
    }

    nonRtServerCtrl.clear();
    nonRtClientCtrl.clear();
    rtClientCtrl.clear();
    audiopool.clear();
}

bool carla_bridge::start(const PluginType type,
                         const char* const binaryArchName,
                         const char* const bridgeBinary,
                         const char* label,
                         const char* filename,
                         const int64_t uniqueId)
{
    UNUSED_PARAMETER(binaryArchName);

    if (process == nullptr)
    {
        process = new water::ChildProcess();
    }
    else if (process->isRunning())
    {
        carla_stderr("CarlaPluginBridgeThread::run() - already running");
    }

    char strBuf[STR_MAX+1];
    strBuf[STR_MAX] = '\0';

    // setup binary arch
    water::ChildProcess::Type childType;
#ifdef CARLA_OS_MAC
    /**/ if (std::strcmp(binaryArchName, "arm64") == 0)
        childType = water::ChildProcess::TypeARM;
    else if (std::strcmp(binaryArchName == "x86_64") == 0)
        childType = water::ChildProcess::TypeIntel;
    else
#endif
        childType = water::ChildProcess::TypeAny;

    // do not use null strings for label and filename
    if (label == nullptr || label[0] == '\0')
        label = "(none)";
    if (filename == nullptr || filename[0] == '\0')
        filename = "(none)";

    water::StringArray arguments;

    // bridge binary
    arguments.add(bridgeBinary);

    // plugin type
    arguments.add(getPluginTypeAsString(type));

    // filename
    arguments.add(filename);

    // label
    arguments.add(label);

    // uniqueId
    arguments.add(water::String(static_cast<water::int64>(uniqueId)));

    bool started;

    {
        const CarlaScopedEnvVar sev("ENGINE_BRIDGE_SHM_IDS", shmIdsStr);

        carla_stdout("Starting plugin bridge, command is:\n%s \"%s\" \"%s\" \"%s\" " P_INT64,
                     bridgeBinary, getPluginTypeAsString(type), filename, label, uniqueId);

        started = process->start(arguments, childType);
    }

    if (! started)
    {
        carla_stdout("failed!");
        process = nullptr;
        return false;
    }

    return true;
}

bool carla_bridge::isRunning() const
{
    return process != nullptr && process->isRunning();
}

void carla_bridge::wait(const char* const action, const uint msecs)
{
        CARLA_SAFE_ASSERT_RETURN(! timedOut,);
//         CARLA_SAFE_ASSERT_RETURN(! fTimedError,);

    if (rtClientCtrl.waitForClient(msecs))
        return;

    timedOut = true;
    carla_stderr2("waitForClient(%s) timed out", action);
}

// --------------------------------------------------------------------------------------------------------------------

void carla_bridge::activate()
{
    {
        const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

        nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientActivate);
        nonRtClientCtrl.commitWrite();
    }

    timedOut = false;

    if (isRunning())
    {
        try {
            wait("activate", 2000);
        } CARLA_SAFE_EXCEPTION("activate - waitForClient");
    }
}

void carla_bridge::deactivate()
{
    {
        const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

        nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientDeactivate);
        nonRtClientCtrl.commitWrite();
    }

    // timedOut = false;

    if (isRunning())
    {
        try {
            wait("deactivate", 2000);
        } CARLA_SAFE_EXCEPTION("deactivate - waitForClient");
    }
}
