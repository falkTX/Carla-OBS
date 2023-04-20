/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-wrapper.h"
#include "qtutils.h"
#include <util/platform.h>

#include "CarlaBackendUtils.hpp"
#include "CarlaBridgeUtils.hpp"
#include "CarlaJuceUtils.hpp"
#include "CarlaScopeUtils.hpp"
#include "CarlaShmUtils.hpp"
#include "CarlaThread.hpp"

#include "jackbridge/JackBridge.hpp"

#include "CarlaFrontend.h"

#include "water/files/File.h"
#include "water/misc/Time.h"
#include "water/threads/ChildProcess.h"

#include <ctime>
#include <vector>

// generates a warning if this is defined as anything else
#define CARLA_API

CARLA_BACKEND_USE_NAMESPACE

// ---------------------------------------------------------------------------------------------------------------------

struct BridgeParamInfo {
    float value;
    CarlaString name;
    CarlaString symbol;
    CarlaString unit;

    BridgeParamInfo() noexcept
        : value(0.0f),
          name(),
          symbol(),
          unit() {}

    CARLA_DECLARE_NON_COPYABLE(BridgeParamInfo)
};

class CarlaPluginBridgeThread : public CarlaThread
{
public:
    CarlaPluginBridgeThread() noexcept
        : CarlaThread("CarlaPluginBridgeThread")
    {
    }

    void setData(const PluginType type,
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

protected:
    void run()
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
// private data methods

struct carla_param_data {
    uint32_t hints;
    float min, max;
};

struct carla_priv {
    obs_source_t *source;
    uint32_t bufferSize;
    double sampleRate;
    bool loaded = false;

    CarlaString             fBridgeBinary;
    CarlaPluginBridgeThread fBridgeThread;

    struct {
        BridgeAudioPool          audiopool; // fShmAudioPool
        BridgeRtClientControl    rtClientCtrl; // fShmRtClientControl
        BridgeNonRtClientControl nonRtClientCtrl; // fShmNonRtClientControl
        BridgeNonRtServerControl nonRtServerCtrl; // fShmNonRtServerControl

        // init sem/shm
        bool init(uint32_t bufferSize, double sampleRate)
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
                audiopool.clear();
                return false;
            }

            if (! nonRtClientCtrl.initializeServer())
            {
                carla_stderr("Failed to initialize Non-RT client control");
                rtClientCtrl.clear();
                audiopool.clear();
                return false;
            }

            if (! nonRtServerCtrl.initializeServer())
            {
                carla_stderr("Failed to initialize Non-RT server control");
                nonRtClientCtrl.clear();
                rtClientCtrl.clear();
                audiopool.clear();
                return false;
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

//             if (audiopool.dataSize != 0)
            {
                rtClientCtrl.writeOpcode(kPluginBridgeRtClientSetAudioPool);
                rtClientCtrl.writeULong(static_cast<uint64_t>(audiopool.dataSize));
                rtClientCtrl.commitWrite();
            }
//             else
//             {
//                 // testing dummy message
//                 rtClientCtrl.writeOpcode(kPluginBridgeRtClientNull);
//                 rtClientCtrl.commitWrite();
//             }

            rtClientCtrl.writeOpcode(kPluginBridgeRtClientSetBufferSize);
            rtClientCtrl.writeUInt(bufferSize);
            rtClientCtrl.commitWrite();

            return true;
        }

        void cleanup()
        {
            nonRtServerCtrl.clear();
            nonRtClientCtrl.clear();
            rtClientCtrl.clear();
            audiopool.clear();
        }
    } bridge;

    struct Info {
        uint32_t aIns, aOuts;
        uint32_t cvIns, cvOuts;
        uint32_t mIns, mOuts;
        PluginCategory category;
        uint optionsAvailable;
        CarlaString name;
        CarlaString label;
        CarlaString maker;
        CarlaString copyright;
        const char** aInNames;
        const char** aOutNames;
        const char** cvInNames;
        const char** cvOutNames;
        std::vector<uint8_t> chunk;

        Info()
            : aIns(0),
              aOuts(0),
              cvIns(0),
              cvOuts(0),
              mIns(0),
              mOuts(0),
              category(PLUGIN_CATEGORY_NONE),
              optionsAvailable(0),
              name(),
              label(),
              maker(),
              copyright(),
              aInNames(nullptr),
              aOutNames(nullptr),
              cvInNames(nullptr),
              cvOutNames(nullptr),
              chunk() {}

        ~Info()
        {
            clear();
        }

        void clear()
        {
            if (aInNames != nullptr)
            {
                CARLA_SAFE_ASSERT_INT(aIns > 0, aIns);

                for (uint32_t i=0; i<aIns; ++i)
                    delete[] aInNames[i];

                delete[] aInNames;
                aInNames = nullptr;
            }

            if (aOutNames != nullptr)
            {
                CARLA_SAFE_ASSERT_INT(aOuts > 0, aOuts);

                for (uint32_t i=0; i<aOuts; ++i)
                    delete[] aOutNames[i];

                delete[] aOutNames;
                aOutNames = nullptr;
            }

            if (cvInNames != nullptr)
            {
                CARLA_SAFE_ASSERT_INT(cvIns > 0, cvIns);

                for (uint32_t i=0; i<cvIns; ++i)
                    delete[] cvInNames[i];

                delete[] cvInNames;
                cvInNames = nullptr;
            }

            if (cvOutNames != nullptr)
            {
                CARLA_SAFE_ASSERT_INT(cvOuts > 0, cvOuts);

                for (uint32_t i=0; i<cvOuts; ++i)
                    delete[] cvOutNames[i];

                delete[] cvOutNames;
                cvOutNames = nullptr;
            }

            aIns = aOuts = cvIns = cvOuts = 0;
        }

        CARLA_DECLARE_NON_COPYABLE(Info)
    } fInfo;

    void handleNonRtData()
    {
        for (; bridge.nonRtServerCtrl.isDataAvailableForReading();)
        {
            const PluginBridgeNonRtServerOpcode opcode(bridge.nonRtServerCtrl.readOpcode());
// #ifdef DEBUG
            if (opcode != kPluginBridgeNonRtServerPong /*&& opcode != kPluginBridgeNonRtServerParameterValue2*/) {
                carla_stdout("CarlaPluginBridge::handleNonRtData() - got opcode: %s", PluginBridgeNonRtServerOpcode2str(opcode));
            }
// #endif
            switch (opcode)
            {
            case kPluginBridgeNonRtServerNull:
            case kPluginBridgeNonRtServerPong:
                break;

            case kPluginBridgeNonRtServerVersion:
                // fBridgeVersion =
                bridge.nonRtServerCtrl.readUInt();
                break;

            case kPluginBridgeNonRtServerPluginInfo1: {
                // uint/category, uint/hints, uint/optionsAvailable, uint/optionsEnabled, long/uniqueId
                const uint32_t category = bridge.nonRtServerCtrl.readUInt();
                const uint32_t hints    = bridge.nonRtServerCtrl.readUInt();
                const uint32_t optionAv = bridge.nonRtServerCtrl.readUInt();
                const uint32_t optionEn = bridge.nonRtServerCtrl.readUInt();
                const  int64_t uniqueId = bridge.nonRtServerCtrl.readLong();

//                 if (fUniqueId != 0) {
//                     CARLA_SAFE_ASSERT_INT2(fUniqueId == uniqueId, fUniqueId, uniqueId);
//                 }

//                 pData->hints   = hints | PLUGIN_IS_BRIDGE;
//                 pData->options = optionEn;

//                #ifdef HAVE_X11
//                 if (fBridgeVersion < 9 || fBinaryType == BINARY_WIN32 || fBinaryType == BINARY_WIN64)
//                #endif
//                 {
//                     pData->hints &= ~PLUGIN_HAS_CUSTOM_EMBED_UI;
//                 }

//                 fInfo.category = static_cast<PluginCategory>(category);
//                 fInfo.optionsAvailable = optionAv;
            }   break;

            case kPluginBridgeNonRtServerPluginInfo2: {
                // uint/size, str[] (realName), uint/size, str[] (label), uint/size, str[] (maker), uint/size, str[] (copyright)

                // realName
                const uint32_t realNameSize(bridge.nonRtServerCtrl.readUInt());
                char realName[realNameSize+1];
                carla_zeroChars(realName, realNameSize+1);
                bridge.nonRtServerCtrl.readCustomData(realName, realNameSize);

                // label
                const uint32_t labelSize(bridge.nonRtServerCtrl.readUInt());
                char label[labelSize+1];
                carla_zeroChars(label, labelSize+1);
                bridge.nonRtServerCtrl.readCustomData(label, labelSize);

                // maker
                const uint32_t makerSize(bridge.nonRtServerCtrl.readUInt());
                char maker[makerSize+1];
                carla_zeroChars(maker, makerSize+1);
                bridge.nonRtServerCtrl.readCustomData(maker, makerSize);

                // copyright
                const uint32_t copyrightSize(bridge.nonRtServerCtrl.readUInt());
                char copyright[copyrightSize+1];
                carla_zeroChars(copyright, copyrightSize+1);
                bridge.nonRtServerCtrl.readCustomData(copyright, copyrightSize);

//                 fInfo.name  = realName;
//                 fInfo.label = label;
//                 fInfo.maker = maker;
//                 fInfo.copyright = copyright;

//                 if (pData->name == nullptr)
//                     pData->name = pData->engine->getUniquePluginName(realName);
            }   break;

            case kPluginBridgeNonRtServerAudioCount: {
                // uint/ins, uint/outs
                fInfo.clear();

                fInfo.aIns  = bridge.nonRtServerCtrl.readUInt();
                fInfo.aOuts = bridge.nonRtServerCtrl.readUInt();

                if (fInfo.aIns > 0)
                {
                    fInfo.aInNames = new const char*[fInfo.aIns];
                    carla_zeroPointers(fInfo.aInNames, fInfo.aIns);
                }

                if (fInfo.aOuts > 0)
                {
                    fInfo.aOutNames = new const char*[fInfo.aOuts];
                    carla_zeroPointers(fInfo.aOutNames, fInfo.aOuts);
                }

            }   break;

            case kPluginBridgeNonRtServerMidiCount: {
                // uint/ins, uint/outs
                fInfo.mIns  = bridge.nonRtServerCtrl.readUInt();
                fInfo.mOuts = bridge.nonRtServerCtrl.readUInt();
            }   break;

            case kPluginBridgeNonRtServerCvCount: {
                // uint/ins, uint/outs
                fInfo.cvIns  = bridge.nonRtServerCtrl.readUInt();
                fInfo.cvOuts = bridge.nonRtServerCtrl.readUInt();
            }   break;

            case kPluginBridgeNonRtServerParameterCount: {
                // uint/count
                const uint32_t count = bridge.nonRtServerCtrl.readUInt();

                // delete old data
//                 pData->param.clear();
//
//                 if (fParams != nullptr)
//                 {
//                     delete[] fParams;
//                     fParams = nullptr;
//                 }
//
//                 if (count > 0)
//                 {
//                     pData->param.createNew(count, false);
//                     fParams = new BridgeParamInfo[count];
//
//                     // we might not receive all parameter data, so ensure range max is not 0
//                     for (uint32_t i=0; i<count; ++i)
//                     {
//                         pData->param.ranges[i].def = 0.0f;
//                         pData->param.ranges[i].min = 0.0f;
//                         pData->param.ranges[i].max = 1.0f;
//                         pData->param.ranges[i].step = 0.001f;
//                         pData->param.ranges[i].stepSmall = 0.0001f;
//                         pData->param.ranges[i].stepLarge = 0.1f;
//                     }
//                 }
            }   break;

            case kPluginBridgeNonRtServerProgramCount: {
                // uint/count
//                 pData->prog.clear();

                if (const uint32_t count = bridge.nonRtServerCtrl.readUInt())
                {}
//                     pData->prog.createNew(count);

            }   break;

            case kPluginBridgeNonRtServerMidiProgramCount: {
                // uint/count
//                 pData->midiprog.clear();

                if (const uint32_t count = bridge.nonRtServerCtrl.readUInt())
                {}
//                     pData->midiprog.createNew(count);

            }   break;

            case kPluginBridgeNonRtServerPortName: {
                // byte/type, uint/index, uint/size, str[] (name)
                const uint8_t  portType = bridge.nonRtServerCtrl.readByte();
                const uint32_t index    = bridge.nonRtServerCtrl.readUInt();

                // name
                const uint32_t nameSize(bridge.nonRtServerCtrl.readUInt());
                char* const name = new char[nameSize+1];
                carla_zeroChars(name, nameSize+1);
                bridge.nonRtServerCtrl.readCustomData(name, nameSize);

                CARLA_SAFE_ASSERT_BREAK(portType > kPluginBridgePortNull && portType < kPluginBridgePortTypeCount);

                switch (portType)
                {
                case kPluginBridgePortAudioInput:
                    CARLA_SAFE_ASSERT_BREAK(index < fInfo.aIns);
                    fInfo.aInNames[index] = name;
                    break;
                case kPluginBridgePortAudioOutput:
                    CARLA_SAFE_ASSERT_BREAK(index < fInfo.aOuts);
                    fInfo.aOutNames[index] = name;
                    break;
                }

            }   break;

            case kPluginBridgeNonRtServerParameterData1: {
                // uint/index, int/rindex, uint/type, uint/hints, int/cc
                const uint32_t index  = bridge.nonRtServerCtrl.readUInt();
                const  int32_t rindex = bridge.nonRtServerCtrl.readInt();
                const uint32_t type   = bridge.nonRtServerCtrl.readUInt();
                const uint32_t hints  = bridge.nonRtServerCtrl.readUInt();
                const  int16_t ctrl   = bridge.nonRtServerCtrl.readShort();

//                 CARLA_SAFE_ASSERT_BREAK(ctrl >= CONTROL_INDEX_NONE && ctrl <= CONTROL_INDEX_MAX_ALLOWED);
//                 CARLA_SAFE_ASSERT_INT2(index < pData->param.count, index, pData->param.count);
//
//                 if (index < pData->param.count)
//                 {
//                     pData->param.data[index].type   = static_cast<ParameterType>(type);
//                     pData->param.data[index].index  = static_cast<int32_t>(index);
//                     pData->param.data[index].rindex = rindex;
//                     pData->param.data[index].hints  = hints;
//                     pData->param.data[index].mappedControlIndex = ctrl;
//                 }
            }   break;

            case kPluginBridgeNonRtServerParameterData2: {
                // uint/index, uint/size, str[] (name), uint/size, str[] (unit)
                const uint32_t index = bridge.nonRtServerCtrl.readUInt();

                // name
                const uint32_t nameSize(bridge.nonRtServerCtrl.readUInt());
                char name[nameSize+1];
                carla_zeroChars(name, nameSize+1);
                bridge.nonRtServerCtrl.readCustomData(name, nameSize);

                // symbol
                const uint32_t symbolSize(bridge.nonRtServerCtrl.readUInt());
                char symbol[symbolSize+1];
                carla_zeroChars(symbol, symbolSize+1);
                bridge.nonRtServerCtrl.readCustomData(symbol, symbolSize);

                // unit
                const uint32_t unitSize(bridge.nonRtServerCtrl.readUInt());
                char unit[unitSize+1];
                carla_zeroChars(unit, unitSize+1);
                bridge.nonRtServerCtrl.readCustomData(unit, unitSize);

//                 CARLA_SAFE_ASSERT_INT2(index < pData->param.count, index, pData->param.count);
//
//                 if (index < pData->param.count)
//                 {
//                     fParams[index].name   = name;
//                     fParams[index].symbol = symbol;
//                     fParams[index].unit   = unit;
//                 }
            }   break;

            case kPluginBridgeNonRtServerParameterRanges: {
                // uint/index, float/def, float/min, float/max, float/step, float/stepSmall, float/stepLarge
                const uint32_t index = bridge.nonRtServerCtrl.readUInt();
                const float def      = bridge.nonRtServerCtrl.readFloat();
                const float min      = bridge.nonRtServerCtrl.readFloat();
                const float max      = bridge.nonRtServerCtrl.readFloat();
                const float step      = bridge.nonRtServerCtrl.readFloat();
                const float stepSmall = bridge.nonRtServerCtrl.readFloat();
                const float stepLarge = bridge.nonRtServerCtrl.readFloat();

//                 CARLA_SAFE_ASSERT_BREAK(min < max);
//                 CARLA_SAFE_ASSERT_BREAK(def >= min);
//                 CARLA_SAFE_ASSERT_BREAK(def <= max);
//                 CARLA_SAFE_ASSERT_INT2(index < pData->param.count, index, pData->param.count);
//
//                 if (index < pData->param.count)
//                 {
//                     pData->param.ranges[index].def = def;
//                     pData->param.ranges[index].min = min;
//                     pData->param.ranges[index].max = max;
//                     pData->param.ranges[index].step      = step;
//                     pData->param.ranges[index].stepSmall = stepSmall;
//                     pData->param.ranges[index].stepLarge = stepLarge;
//                 }
            }   break;

            case kPluginBridgeNonRtServerParameterValue: {
                // uint/index, float/value
                const uint32_t index = bridge.nonRtServerCtrl.readUInt();
                const float    value = bridge.nonRtServerCtrl.readFloat();

//                 if (index < pData->param.count)
//                 {
//                     const float fixedValue(pData->param.getFixedValue(index, value));
//
//                     if (carla_isNotEqual(fParams[index].value, fixedValue))
//                     {
//                         fParams[index].value = fixedValue;
//                         CarlaPlugin::setParameterValue(index, fixedValue, false, true, true);
//                     }
//                 }
            }   break;

            case kPluginBridgeNonRtServerParameterValue2: {
                // uint/index, float/value
                const uint32_t index = bridge.nonRtServerCtrl.readUInt();
                const float    value = bridge.nonRtServerCtrl.readFloat();

//                 if (index < pData->param.count)
//                 {
//                     const float fixedValue(pData->param.getFixedValue(index, value));
//                     fParams[index].value = fixedValue;
//                 }
            }   break;

            case kPluginBridgeNonRtServerParameterTouch: {
                // uint/index, bool/touch
                const uint32_t index = bridge.nonRtServerCtrl.readUInt();
                const bool     touch = bridge.nonRtServerCtrl.readBool();

//                 pData->engine->touchPluginParameter(pData->id, index, touch);
            }   break;

            case kPluginBridgeNonRtServerDefaultValue: {
                // uint/index, float/value
                const uint32_t index = bridge.nonRtServerCtrl.readUInt();
                const float    value = bridge.nonRtServerCtrl.readFloat();

//                 if (index < pData->param.count)
//                     pData->param.ranges[index].def = value;
            }   break;

            case kPluginBridgeNonRtServerCurrentProgram: {
                // int/index
                const int32_t index = bridge.nonRtServerCtrl.readInt();

//                 CARLA_SAFE_ASSERT_BREAK(index >= -1);
//                 CARLA_SAFE_ASSERT_INT2(index < static_cast<int32_t>(pData->prog.count), index, pData->prog.count);

//                 CarlaPlugin::setProgram(index, false, true, true);
            }   break;

            case kPluginBridgeNonRtServerCurrentMidiProgram: {
                // int/index
                const int32_t index = bridge.nonRtServerCtrl.readInt();

//                 CARLA_SAFE_ASSERT_BREAK(index >= -1);
//                 CARLA_SAFE_ASSERT_INT2(index < static_cast<int32_t>(pData->midiprog.count), index, pData->midiprog.count);

//                 CarlaPlugin::setMidiProgram(index, false, true, true);
            }   break;

            case kPluginBridgeNonRtServerProgramName: {
                // uint/index, uint/size, str[] (name)
                const uint32_t index = bridge.nonRtServerCtrl.readUInt();

                // name
                const uint32_t nameSize(bridge.nonRtServerCtrl.readUInt());
                char name[nameSize+1];
                carla_zeroChars(name, nameSize+1);
                bridge.nonRtServerCtrl.readCustomData(name, nameSize);

//                 CARLA_SAFE_ASSERT_INT2(index < pData->prog.count, index, pData->prog.count);
//
//                 if (index < pData->prog.count)
//                 {
//                     if (pData->prog.names[index] != nullptr)
//                         delete[] pData->prog.names[index];
//                     pData->prog.names[index] = carla_strdup(name);
//                 }
            }   break;

            case kPluginBridgeNonRtServerMidiProgramData: {
                // uint/index, uint/bank, uint/program, uint/size, str[] (name)
                const uint32_t index   = bridge.nonRtServerCtrl.readUInt();
                const uint32_t bank    = bridge.nonRtServerCtrl.readUInt();
                const uint32_t program = bridge.nonRtServerCtrl.readUInt();

                // name
                const uint32_t nameSize(bridge.nonRtServerCtrl.readUInt());
                char name[nameSize+1];
                carla_zeroChars(name, nameSize+1);
                bridge.nonRtServerCtrl.readCustomData(name, nameSize);

//                 CARLA_SAFE_ASSERT_INT2(index < pData->midiprog.count, index, pData->midiprog.count);
//
//                 if (index < pData->midiprog.count)
//                 {
//                     if (pData->midiprog.data[index].name != nullptr)
//                         delete[] pData->midiprog.data[index].name;
//                     pData->midiprog.data[index].bank    = bank;
//                     pData->midiprog.data[index].program = program;
//                     pData->midiprog.data[index].name    = carla_strdup(name);
//                 }
            }   break;

            case kPluginBridgeNonRtServerSetCustomData: {
                // uint/size, str[], uint/size, str[], uint/size, str[]

                // type
                const uint32_t typeSize = bridge.nonRtServerCtrl.readUInt();
                char type[typeSize+1];
                carla_zeroChars(type, typeSize+1);
                bridge.nonRtServerCtrl.readCustomData(type, typeSize);

                // key
                const uint32_t keySize = bridge.nonRtServerCtrl.readUInt();
                char key[keySize+1];
                carla_zeroChars(key, keySize+1);
                bridge.nonRtServerCtrl.readCustomData(key, keySize);

                // value
                const uint32_t valueSize = bridge.nonRtServerCtrl.readUInt();

                // special case for big values
//                 if (valueSize > 16384)
//                 {
//                     const uint32_t bigValueFilePathSize = bridge.nonRtServerCtrl.readUInt();
//                     char bigValueFilePath[bigValueFilePathSize+1];
//                     carla_zeroChars(bigValueFilePath, bigValueFilePathSize+1);
//                     bridge.nonRtServerCtrl.readCustomData(bigValueFilePath, bigValueFilePathSize);
//
//                     String realBigValueFilePath(bigValueFilePath);
//
// #ifndef CARLA_OS_WIN
//                     // Using Wine, fix temp dir
//                     if (fBinaryType == BINARY_WIN32 || fBinaryType == BINARY_WIN64)
//                     {
//                         const StringArray driveLetterSplit(StringArray::fromTokens(realBigValueFilePath, ":/", ""));
//                         carla_stdout("big value save path BEFORE => %s", realBigValueFilePath.toRawUTF8());
//
//                         realBigValueFilePath  = fWinePrefix;
//                         realBigValueFilePath += "/drive_";
//                         realBigValueFilePath += driveLetterSplit[0].toLowerCase();
//                         realBigValueFilePath += driveLetterSplit[1];
//
//                         realBigValueFilePath  = realBigValueFilePath.replace("\\", "/");
//                         carla_stdout("big value save path AFTER => %s", realBigValueFilePath.toRawUTF8());
//                     }
// #endif
//
//                     const File bigValueFile(realBigValueFilePath);
//                     CARLA_SAFE_ASSERT_BREAK(bigValueFile.existsAsFile());
//
//                     CarlaPlugin::setCustomData(type, key, bigValueFile.loadFileAsString().toRawUTF8(), false);
//
//                     bigValueFile.deleteFile();
//                 }
//                 else
//                 {
                    char value[valueSize+1];
                    carla_zeroChars(value, valueSize+1);

                    if (valueSize > 0)
                        bridge.nonRtServerCtrl.readCustomData(value, valueSize);

//                     CarlaPlugin::setCustomData(type, key, value, false);
//                 }

            }   break;

            case kPluginBridgeNonRtServerSetChunkDataFile: {
                // uint/size, str[] (filename)

                // chunkFilePath
                const uint32_t chunkFilePathSize = bridge.nonRtServerCtrl.readUInt();
                char chunkFilePath[chunkFilePathSize+1];
                carla_zeroChars(chunkFilePath, chunkFilePathSize+1);
                bridge.nonRtServerCtrl.readCustomData(chunkFilePath, chunkFilePathSize);

//                 String realChunkFilePath(chunkFilePath);
//
// #ifndef CARLA_OS_WIN
//                 // Using Wine, fix temp dir
//                 if (fBinaryType == BINARY_WIN32 || fBinaryType == BINARY_WIN64)
//                 {
//                     const StringArray driveLetterSplit(StringArray::fromTokens(realChunkFilePath, ":/", ""));
//                     carla_stdout("chunk save path BEFORE => %s", realChunkFilePath.toRawUTF8());
//
//                     realChunkFilePath  = fWinePrefix;
//                     realChunkFilePath += "/drive_";
//                     realChunkFilePath += driveLetterSplit[0].toLowerCase();
//                     realChunkFilePath += driveLetterSplit[1];
//
//                     realChunkFilePath  = realChunkFilePath.replace("\\", "/");
//                     carla_stdout("chunk save path AFTER => %s", realChunkFilePath.toRawUTF8());
//                 }
// #endif

//                 const File chunkFile(realChunkFilePath);
//                 CARLA_SAFE_ASSERT_BREAK(chunkFile.existsAsFile());
//
//                 fInfo.chunk = carla_getChunkFromBase64String(chunkFile.loadFileAsString().toRawUTF8());
//                 chunkFile.deleteFile();
            }   break;

            case kPluginBridgeNonRtServerSetLatency:
                // uint
//                 fLatency =
                bridge.nonRtServerCtrl.readUInt();
// #ifndef BUILD_BRIDGE
//                 if (! fInitiated)
//                     pData->latency.recreateBuffers(std::max(fInfo.aIns, fInfo.aOuts), fLatency);
// #endif
                break;

            case kPluginBridgeNonRtServerSetParameterText: {
                const int32_t index = bridge.nonRtServerCtrl.readInt();

                const uint32_t textSize(bridge.nonRtServerCtrl.readUInt());
                char text[textSize+1];
                carla_zeroChars(text, textSize+1);
                bridge.nonRtServerCtrl.readCustomData(text, textSize);

//                 fReceivingParamText.setReceivedData(index, text, textSize);
            }   break;

            case kPluginBridgeNonRtServerReady:
                loaded = true;
                break;

            case kPluginBridgeNonRtServerSaved:
//                 fSaved = true;
                break;

            case kPluginBridgeNonRtServerRespEmbedUI:
//                 fPendingEmbedCustomUI =
                bridge.nonRtServerCtrl.readULong();
                break;

            case kPluginBridgeNonRtServerResizeEmbedUI: {
                const uint width = bridge.nonRtServerCtrl.readUInt();
                const uint height = bridge.nonRtServerCtrl.readUInt();
//                 pData->engine->callback(true, true, ENGINE_CALLBACK_EMBED_UI_RESIZED, pData->id,
//                                         static_cast<int>(width), static_cast<int>(height),
//                                         0, 0.0f, nullptr);
            }   break;

            case kPluginBridgeNonRtServerUiClosed:
// #ifndef BUILD_BRIDGE_ALTERNATIVE_ARCH
//                 pData->transientTryCounter = 0;
// #endif
//                 pData->engine->callback(true, true, ENGINE_CALLBACK_UI_STATE_CHANGED, pData->id,
//                                         0, 0, 0, 0.0f, nullptr);
                break;

            case kPluginBridgeNonRtServerError: {
                // error
                const uint32_t errorSize(bridge.nonRtServerCtrl.readUInt());
                char error[errorSize+1];
                carla_zeroChars(error, errorSize+1);
                bridge.nonRtServerCtrl.readCustomData(error, errorSize);

//                 if (fInitiated)
//                 {
//                     pData->engine->callback(true, true, ENGINE_CALLBACK_ERROR, pData->id, 0, 0, 0, 0.0f, error);
//
//                     // just in case
//                     pData->engine->setLastError(error);
//                     fInitError = true;
//                 }
//                 else
//                 {
//                     pData->engine->setLastError(error);
//                     fInitError = true;
//                     fInitiated = true;
//                 }
            }   break;
            }
        }
    }

    void waitForClient(const char* const action, const uint msecs)
    {
//         CARLA_SAFE_ASSERT_RETURN(! fTimedOut,);
//         CARLA_SAFE_ASSERT_RETURN(! fTimedError,);

        if (bridge.rtClientCtrl.waitForClient(msecs))
            return;

//         fTimedOut = true;
        carla_stderr2("waitForClient(%s) timed out", action);
    }

};

// --------------------------------------------------------------------------------------------------------------------
// carla + obs integration methods

struct carla_priv *carla_priv_create(obs_source_t *source, enum buffer_size_mode bufsize, uint32_t srate, bool filter)
{
    struct carla_priv *priv = new struct carla_priv;
    if (priv == NULL)
        return NULL;

    priv->source = source;
    priv->bufferSize = MAX_AUDIO_BUFFER_SIZE; // bufsize_mode_to_frames(bufsize);
    priv->sampleRate = srate;

    assert(priv->bufferSize != 0);
    if (priv->bufferSize == 0)
        goto fail1;

    priv->bridge.init(priv->bufferSize, srate);

    return priv;

fail1:
    delete priv;
    return nullptr;
}

void carla_priv_destroy(struct carla_priv *priv)
{
    if (priv->fBridgeThread.isThreadRunning())
    {
        priv->bridge.nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientQuit);
        priv->bridge.nonRtClientCtrl.commitWrite();

        priv->bridge.rtClientCtrl.writeOpcode(kPluginBridgeRtClientQuit);
        priv->bridge.rtClientCtrl.commitWrite();

//         if (! fTimedOut)
            priv->waitForClient("stopping", 3000);

        priv->fBridgeThread.stopThread(3000);
    }

    priv->bridge.cleanup();
    delete priv;
}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_activate(struct carla_priv *priv)
{
    {
        const CarlaMutexLocker _cml(priv->bridge.nonRtClientCtrl.mutex);

        priv->bridge.nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientActivate);
        priv->bridge.nonRtClientCtrl.commitWrite();
    }

    if (priv->fBridgeThread.isThreadRunning())
    {
        try {
            priv->waitForClient("activate", 2000);
        } CARLA_SAFE_EXCEPTION("activate - waitForClient");
    }
}

void carla_priv_deactivate(struct carla_priv *priv)
{
    {
        const CarlaMutexLocker _cml(priv->bridge.nonRtClientCtrl.mutex);

        priv->bridge.nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientDeactivate);
        priv->bridge.nonRtClientCtrl.commitWrite();
    }

//     fTimedOut = false;

    if (priv->fBridgeThread.isThreadRunning())
    {
        try {
            priv->waitForClient("deactivate", 2000);
        } CARLA_SAFE_EXCEPTION("deactivate - waitForClient");
    }
}

void carla_priv_process_audio(struct carla_priv *priv, float *buffers[2], uint32_t frames)
{
    if (!priv->fBridgeThread.isThreadRunning())
        return;

    for (uint32_t c=0; c < 2; ++c)
        carla_copyFloats(priv->bridge.audiopool.data + (c * MAX_AUDIO_BUFFER_SIZE), buffers[c], frames);

    {
        priv->bridge.rtClientCtrl.writeOpcode(kPluginBridgeRtClientProcess);
        priv->bridge.rtClientCtrl.writeUInt(frames);
        priv->bridge.rtClientCtrl.commitWrite();
    }

    priv->waitForClient("process", 1000);

}

void carla_priv_idle(struct carla_priv *priv)
{
    if (priv->fBridgeThread.isThreadRunning())
    {
//         if (priv->loaded && fTimedOut && pData->active)
//             setActive(false, true, true);

        {
            const CarlaMutexLocker _cml(priv->bridge.nonRtClientCtrl.mutex);

            priv->bridge.nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientPing);
            priv->bridge.nonRtClientCtrl.commitWrite();
        }

        try {
            priv->handleNonRtData();
        } CARLA_SAFE_EXCEPTION("handleNonRtData");
    }
    else if (priv->loaded)
    {
//         fTimedOut   = true;
//         fTimedError = true;
        priv->loaded  = false;
//         handleProcessStopped();
    }
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
    if (priv->fBridgeThread.isThreadRunning())
    {
        obs_properties_add_button2(props, PROP_SHOW_GUI, obs_module_text("Show custom GUI"),
                                   carla_priv_show_gui_callback, priv);
    }

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

    {
        char shmIdsStr[6*4+1];
        carla_zeroChars(shmIdsStr, 6*4+1);

        std::strncpy(shmIdsStr+6*0, &priv->bridge.audiopool.filename[priv->bridge.audiopool.filename.length()-6], 6);
        std::strncpy(shmIdsStr+6*1, &priv->bridge.rtClientCtrl.filename[priv->bridge.rtClientCtrl.filename.length()-6], 6);
        std::strncpy(shmIdsStr+6*2, &priv->bridge.nonRtClientCtrl.filename[priv->bridge.nonRtClientCtrl.filename.length()-6], 6);
        std::strncpy(shmIdsStr+6*3, &priv->bridge.nonRtServerCtrl.filename[priv->bridge.nonRtServerCtrl.filename.length()-6], 6);

        priv->fBridgeThread.setData((PluginType)plugin->type,
                                    "x86_64",
                                    "/usr/lib/carla/carla-bridge-native",
                                    plugin->label,
                                    plugin->filename,
                                    plugin->uniqueId,
                                    shmIdsStr);
    }

    priv->fBridgeThread.startThread();

    for (;priv->fBridgeThread.isThreadRunning();)
    {
        carla_priv_idle(priv);

        if (priv->loaded)
            break;

        carla_msleep(5);
    }

    priv->bridge.nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientActivate);
    priv->bridge.nonRtClientCtrl.commitWrite();

    return carla_post_load_callback(priv, props);
}

bool carla_priv_show_gui_callback(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct carla_priv *priv = static_cast<struct carla_priv*>(data);

    if (priv->fBridgeThread.isThreadRunning())
    {
        const CarlaMutexLocker _cml(priv->bridge.nonRtClientCtrl.mutex);

        priv->bridge.nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientShowUI);
        priv->bridge.nonRtClientCtrl.commitWrite();
    }

    return false;
}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_free(void *data)
{
    free(data);
}

// --------------------------------------------------------------------------------------------------------------------
