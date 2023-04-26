/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-bridge.h"
#include "carla-wrapper.h"

#include "CarlaBackendUtils.hpp"

#include "water/files/File.h"
#include "water/misc/Time.h"

#include <ctime>

// --------------------------------------------------------------------------------------------------------------------

CarlaPluginBridgeThread::CarlaPluginBridgeThread() noexcept : CarlaThread("CarlaPluginBridgeThread")
{
}

void CarlaPluginBridgeThread::setData(const PluginType type,
                                      const char* const binaryArchName,
                                      const char* const bridgeBinary,
                                      const char* const label,
                                      const char* const filename,
                                      const int64_t uniqueId,
                                      const char* const shmIds) noexcept
{
    CARLA_SAFE_ASSERT_RETURN(bridgeBinary != nullptr && bridgeBinary[0] != '\0',);
    CARLA_SAFE_ASSERT_RETURN(shmIds != nullptr && shmIds[0] != '\0',);
    CARLA_SAFE_ASSERT(! isThreadRunning());

    fPluginType = type;
    fPluginUniqueId = uniqueId;
    fBinaryArchName = binaryArchName;
    fBridgeBinary = bridgeBinary;
    fShmIds = shmIds;

    if (label != nullptr)
        fPluginLabel = label;
    if (fPluginLabel.isEmpty())
        fPluginLabel = "(none)";

    if (filename != nullptr)
        fPluginFilename = filename;
    if (fPluginFilename.isEmpty())
        fPluginFilename = "(none)";
}

void CarlaPluginBridgeThread::run()
{
    if (fProcess == nullptr)
    {
        fProcess = new water::ChildProcess();
    }
    else if (fProcess->isRunning())
    {
        carla_stderr("CarlaPluginBridgeThread::run() - already running");
    }

    char strBuf[STR_MAX+1];
    strBuf[STR_MAX] = '\0';

    // setup binary arch
    water::ChildProcess::Type childType;
#ifdef CARLA_OS_MAC
    if (fBinaryArchName == "arm64")
        childType = water::ChildProcess::TypeARM;
    else if (fBinaryArchName == "x86_64")
        childType = water::ChildProcess::TypeIntel;
    else
#endif
        childType = water::ChildProcess::TypeAny;

    water::StringArray arguments;

    // bridge binary
    arguments.add(fBridgeBinary);

    // plugin type
    arguments.add(getPluginTypeAsString(fPluginType));

    // filename
    arguments.add(fPluginFilename);

    // label
    arguments.add(fPluginLabel);

    // uniqueId
    arguments.add(water::String(static_cast<water::int64>(fPluginUniqueId)));

    bool started;

    {
        const CarlaScopedEnvVar sev("ENGINE_BRIDGE_SHM_IDS", fShmIds.toRawUTF8());

        carla_stdout("Starting plugin bridge, command is:\n%s \"%s\" \"%s\" \"%s\" " P_INT64,
                        fBridgeBinary.toRawUTF8(),
                        getPluginTypeAsString(fPluginType),
                        fPluginFilename.toRawUTF8(),
                        fPluginLabel.toRawUTF8(),
                        fPluginUniqueId);

        started = fProcess->start(arguments, childType);
    }

    if (! started)
    {
        carla_stdout("failed!");
        fProcess = nullptr;
        return;
    }

    for (; fProcess->isRunning() && ! shouldThreadExit();)
        carla_sleep(1);

    // we only get here if bridge crashed or thread asked to exit
    if (fProcess->isRunning() && shouldThreadExit())
    {
        fProcess->waitForProcessToFinish(2000);

        if (fProcess->isRunning())
        {
            carla_stdout("CarlaPluginBridgeThread::run() - bridge refused to close, force kill now");
            fProcess->kill();
        }
        else
        {
            carla_stdout("CarlaPluginBridgeThread::run() - bridge auto-closed successfully");
        }
    }
    else
    {
        // forced quit, may have crashed
        if (fProcess->getExitCodeAndClearPID() != 0)
        {
            carla_stderr("CarlaPluginBridgeThread::run() - bridge crashed");

            CarlaString errorString("Plugin has crashed!\n"
                                    "Saving now will lose its current settings.\n"
                                    "Please remove this plugin, and not rely on it from this point.");
        }
    }

    fProcess = nullptr;
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

    audiopool.resize(MAX_AUDIO_BUFFER_SIZE, MAX_AV_PLANES, MAX_AV_PLANES);

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

    return true;

fail3:
    nonRtClientCtrl.clear();

fail2:
    rtClientCtrl.clear();

fail1:
    audiopool.clear();
    return false;
}

// --------------------------------------------------------------------------------------------------------------------

void carla_bridge::cleanup()
{
    nonRtServerCtrl.clear();
    nonRtClientCtrl.clear();
    rtClientCtrl.clear();
    audiopool.clear();
}

// --------------------------------------------------------------------------------------------------------------------

static void waitForClient(struct carla_bridge *bridge, const char* const action, const uint msecs)
{
//         CARLA_SAFE_ASSERT_RETURN(! fTimedOut,);
//         CARLA_SAFE_ASSERT_RETURN(! fTimedError,);

    if (bridge->rtClientCtrl.waitForClient(msecs))
        return;

//         fTimedOut = true;
    carla_stderr2("waitForClient(%s) timed out", action);
}

// --------------------------------------------------------------------------------------------------------------------

void carla_bridge_destroy(struct carla_bridge *bridge)
{
    if (bridge->thread.isThreadRunning())
    {
        bridge->nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientQuit);
        bridge->nonRtClientCtrl.commitWrite();

        bridge->rtClientCtrl.writeOpcode(kPluginBridgeRtClientQuit);
        bridge->rtClientCtrl.commitWrite();

//         if (! fTimedOut)
            waitForClient(bridge, "stopping", 3000);

        bridge->thread.stopThread(3000);
    }

    bridge->cleanup();
}

void carla_bridge_activate(struct carla_bridge *bridge)
{
    {
        const CarlaMutexLocker _cml(bridge->nonRtClientCtrl.mutex);

        bridge->nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientActivate);
        bridge->nonRtClientCtrl.commitWrite();
    }

    if (bridge->thread.isThreadRunning())
    {
        try {
            waitForClient(bridge, "activate", 2000);
        } CARLA_SAFE_EXCEPTION("activate - waitForClient");
    }
}

void carla_bridge_deactivate(struct carla_bridge *bridge)
{
    {
        const CarlaMutexLocker _cml(bridge->nonRtClientCtrl.mutex);

        bridge->nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientDeactivate);
        bridge->nonRtClientCtrl.commitWrite();
    }

//     fTimedOut = false;

    if (bridge->thread.isThreadRunning())
    {
        try {
            waitForClient(bridge, "deactivate", 2000);
        } CARLA_SAFE_EXCEPTION("deactivate - waitForClient");
    }
}
