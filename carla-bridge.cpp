/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-bridge.hpp"

#include <obs-module.h>
#include <util/platform.h>

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

bool carla_bridge::init(uint32_t maxBufferSize, double sampleRate)
{
    std::srand(static_cast<uint>(std::time(nullptr)));

    if (! audiopool.initializeServer())
    {
        carla_stderr("Failed to initialize shared memory audio pool");
        return false;
    }

    audiopool.resize(maxBufferSize, MAX_AV_PLANES, MAX_AV_PLANES);

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
    nonRtClientCtrl.writeUInt(maxBufferSize);
    nonRtClientCtrl.writeDouble(sampleRate);

    nonRtClientCtrl.commitWrite();

    rtClientCtrl.writeOpcode(kPluginBridgeRtClientSetAudioPool);
    rtClientCtrl.writeULong(static_cast<uint64_t>(audiopool.dataSize));
    rtClientCtrl.commitWrite();

    rtClientCtrl.writeOpcode(kPluginBridgeRtClientSetBufferSize);
    rtClientCtrl.writeUInt(maxBufferSize);
    rtClientCtrl.commitWrite();

    carla_zeroStruct(shmIdsStr);
    std::strncpy(shmIdsStr+6*0, &audiopool.filename[audiopool.filename.length()-6], 6);
    std::strncpy(shmIdsStr+6*1, &rtClientCtrl.filename[rtClientCtrl.filename.length()-6], 6);
    std::strncpy(shmIdsStr+6*2, &nonRtClientCtrl.filename[nonRtClientCtrl.filename.length()-6], 6);
    std::strncpy(shmIdsStr+6*3, &nonRtServerCtrl.filename[nonRtServerCtrl.filename.length()-6], 6);

    bufferSize = maxBufferSize;
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
    ready = false;

    if (childprocess != nullptr)
    {
        if (childprocess->isRunning())
        {
            nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientQuit);
            nonRtClientCtrl.commitWrite();

            rtClientCtrl.writeOpcode(kPluginBridgeRtClientQuit);
            rtClientCtrl.commitWrite();

            if (! timedOut)
                wait("stopping", 3000);
        }

        stopProcess(childprocess);
        childprocess = nullptr;
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

    if (childprocess == nullptr)
    {
        childprocess = new water::ChildProcess();
    }
    else if (childprocess->isRunning())
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

        started = childprocess->start(arguments, childType);
    }

    if (! started)
    {
        carla_stdout("failed!");
        childprocess = nullptr;
        return false;
    }

    return true;
}

bool carla_bridge::isRunning() const
{
    return childprocess != nullptr && childprocess->isRunning();
}

bool carla_bridge::isReady() const noexcept
{
    return ready;
}

bool carla_bridge::idle()
{
    if (childprocess == nullptr)
        return false;

    if (childprocess->isRunning())
    {
        const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

        nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientPing);
        nonRtClientCtrl.commitWrite();
    }
    else
    {
//         fTimedOut   = true;
//         fTimedError = true;
        ready = false;
        stopProcess(childprocess);
        childprocess = nullptr;
        return false;
    }

//         if (priv->loaded && fTimedOut && pData->active)
//             setActive(false, true, true);

    try {
        for (; nonRtServerCtrl.isDataAvailableForReading();)
        {
            const PluginBridgeNonRtServerOpcode opcode(nonRtServerCtrl.readOpcode());
// #ifdef DEBUG
            if (opcode != kPluginBridgeNonRtServerPong && opcode != kPluginBridgeNonRtServerParameterValue2) {
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
                nonRtServerCtrl.readUInt();
                break;

            case kPluginBridgeNonRtServerPluginInfo1: {
                // uint/category, uint/hints, uint/optionsAvailable, uint/optionsEnabled, long/uniqueId
                const uint32_t category = nonRtServerCtrl.readUInt();
                const uint32_t hints    = nonRtServerCtrl.readUInt();
                const uint32_t optionAv = nonRtServerCtrl.readUInt();
                const uint32_t optionEn = nonRtServerCtrl.readUInt();
                const  int64_t uniqueId = nonRtServerCtrl.readLong();

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
                const uint32_t realNameSize(nonRtServerCtrl.readUInt());
                char realName[realNameSize+1];
                carla_zeroChars(realName, realNameSize+1);
                nonRtServerCtrl.readCustomData(realName, realNameSize);

                // label
                const uint32_t labelSize(nonRtServerCtrl.readUInt());
                char label[labelSize+1];
                carla_zeroChars(label, labelSize+1);
                nonRtServerCtrl.readCustomData(label, labelSize);

                // maker
                const uint32_t makerSize(nonRtServerCtrl.readUInt());
                char maker[makerSize+1];
                carla_zeroChars(maker, makerSize+1);
                nonRtServerCtrl.readCustomData(maker, makerSize);

                // copyright
                const uint32_t copyrightSize(nonRtServerCtrl.readUInt());
                char copyright[copyrightSize+1];
                carla_zeroChars(copyright, copyrightSize+1);
                nonRtServerCtrl.readCustomData(copyright, copyrightSize);

                fInfo.name  = realName;
                fInfo.label = label;
                fInfo.maker = maker;
                fInfo.copyright = copyright;
            }   break;

            case kPluginBridgeNonRtServerAudioCount: {
                // uint/ins, uint/outs
                fInfo.clear();

                fInfo.aIns  = nonRtServerCtrl.readUInt();
                fInfo.aOuts = nonRtServerCtrl.readUInt();

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
                fInfo.mIns  = nonRtServerCtrl.readUInt();
                fInfo.mOuts = nonRtServerCtrl.readUInt();
            }   break;

            case kPluginBridgeNonRtServerCvCount: {
                // uint/ins, uint/outs
                fInfo.cvIns  = nonRtServerCtrl.readUInt();
                fInfo.cvOuts = nonRtServerCtrl.readUInt();
            }   break;

            case kPluginBridgeNonRtServerParameterCount: {
                // uint/count
                paramCount = nonRtServerCtrl.readUInt();

                delete[] paramDetails;

                if (paramCount != 0)
                    paramDetails = new carla_param_data[paramCount];
                else
                    paramDetails = nullptr;

            }   break;

            case kPluginBridgeNonRtServerProgramCount: {
                // uint/count
                nonRtServerCtrl.readUInt();
            }   break;

            case kPluginBridgeNonRtServerMidiProgramCount: {
                // uint/count
                nonRtServerCtrl.readUInt();
            }   break;

            case kPluginBridgeNonRtServerPortName: {
                // byte/type, uint/index, uint/size, str[] (name)
                nonRtServerCtrl.readByte();
                nonRtServerCtrl.readUInt();

                // name
                const uint32_t nameSize(nonRtServerCtrl.readUInt());
                char* const name = new char[nameSize+1];
                carla_zeroChars(name, nameSize+1);
                nonRtServerCtrl.readCustomData(name, nameSize);

            }   break;

            case kPluginBridgeNonRtServerParameterData1: {
                // uint/index, int/rindex, uint/type, uint/hints, int/cc
                const uint32_t index  = nonRtServerCtrl.readUInt();
                const  int32_t rindex = nonRtServerCtrl.readInt();
                const uint32_t type   = nonRtServerCtrl.readUInt();
                const uint32_t hints  = nonRtServerCtrl.readUInt();
                const  int16_t ctrl   = nonRtServerCtrl.readShort();

                CARLA_SAFE_ASSERT_INT_BREAK(ctrl >= CONTROL_INDEX_NONE && ctrl <= CONTROL_INDEX_MAX_ALLOWED, ctrl);
                CARLA_SAFE_ASSERT_UINT2_BREAK(index < paramCount, index, paramCount);

                if (type != PARAMETER_INPUT)
                    break;
                if ((hints & PARAMETER_IS_ENABLED) == 0)
                    break;
                if (hints & (PARAMETER_IS_READ_ONLY|PARAMETER_IS_NOT_SAVED))
                    break;

                paramDetails[index].hints = hints;
            }   break;

            case kPluginBridgeNonRtServerParameterData2: {
                // uint/index, uint/size, str[] (name), uint/size, str[] (unit)
                const uint32_t index = nonRtServerCtrl.readUInt();

                // name
                const uint32_t nameSize(nonRtServerCtrl.readUInt());
                char name[nameSize+1];
                carla_zeroChars(name, nameSize+1);
                nonRtServerCtrl.readCustomData(name, nameSize);

                // symbol
                const uint32_t symbolSize(nonRtServerCtrl.readUInt());
                char symbol[symbolSize+1];
                carla_zeroChars(symbol, symbolSize+1);
                nonRtServerCtrl.readCustomData(symbol, symbolSize);

                // unit
                const uint32_t unitSize(nonRtServerCtrl.readUInt());
                char unit[unitSize+1];
                carla_zeroChars(unit, unitSize+1);
                nonRtServerCtrl.readCustomData(unit, unitSize);

                CARLA_SAFE_ASSERT_UINT2_BREAK(index < paramCount, index, paramCount);

                if (paramDetails[index].hints & PARAMETER_IS_ENABLED)
                {
                    paramDetails[index].name   = name;
                    paramDetails[index].symbol = symbol;
                    paramDetails[index].unit   = unit;
                }
            }   break;

            case kPluginBridgeNonRtServerParameterRanges: {
                // uint/index, float/def, float/min, float/max, float/step, float/stepSmall, float/stepLarge
                const uint32_t index = nonRtServerCtrl.readUInt();
                const float def      = nonRtServerCtrl.readFloat();
                const float min      = nonRtServerCtrl.readFloat();
                const float max      = nonRtServerCtrl.readFloat();
                const float step      = nonRtServerCtrl.readFloat();
                nonRtServerCtrl.readFloat();
                nonRtServerCtrl.readFloat();

                CARLA_SAFE_ASSERT_BREAK(min < max);
                CARLA_SAFE_ASSERT_BREAK(def >= min);
                CARLA_SAFE_ASSERT_BREAK(def <= max);
                CARLA_SAFE_ASSERT_UINT2_BREAK(index < paramCount, index, paramCount);

                if (paramDetails[index].hints & PARAMETER_IS_ENABLED)
                {
                    paramDetails[index].def = paramDetails[index].value = def;
                    paramDetails[index].min = min;
                    paramDetails[index].max = max;
                    paramDetails[index].step = step;
                }
            }   break;

            case kPluginBridgeNonRtServerParameterValue: {
                // uint/index, float/value
                const uint32_t index = nonRtServerCtrl.readUInt();
                const float    value = nonRtServerCtrl.readFloat();

                if (index < paramCount)
                {
                    const float fixedValue = carla_fixedValue(paramDetails[index].min, paramDetails[index].max, value);

                    if (carla_isNotEqual(paramDetails[index].value, fixedValue))
                    {
                        paramDetails[index].value = fixedValue;

                        if (callback != nullptr)
                        {
                            // skip parameters that we do not show
                            if ((paramDetails[index].hints & PARAMETER_IS_ENABLED) == 0)
                                break;

                            callback->bridge_parameter_changed(index, fixedValue);
                        }
                    }
                }
            }   break;

            case kPluginBridgeNonRtServerParameterValue2: {
                // uint/index, float/value
                const uint32_t index = nonRtServerCtrl.readUInt();
                const float    value = nonRtServerCtrl.readFloat();

                if (index < paramCount)
                {
                    const float fixedValue = carla_fixedValue(paramDetails[index].min, paramDetails[index].max, value);
                    paramDetails[index].value = fixedValue;
                }
            }   break;

            case kPluginBridgeNonRtServerParameterTouch: {
                // uint/index, bool/touch
                nonRtServerCtrl.readUInt();
                nonRtServerCtrl.readBool();
            }   break;

            case kPluginBridgeNonRtServerDefaultValue: {
                // uint/index, float/value
                const uint32_t index = nonRtServerCtrl.readUInt();
                const float    value = nonRtServerCtrl.readFloat();

                if (index < paramCount)
                    paramDetails[index].def = value;
            }   break;

            case kPluginBridgeNonRtServerCurrentProgram: {
                // int/index
                nonRtServerCtrl.readInt();
            }   break;

            case kPluginBridgeNonRtServerCurrentMidiProgram: {
                // int/index
                nonRtServerCtrl.readInt();
            }   break;

            case kPluginBridgeNonRtServerProgramName: {
                // uint/index, uint/size, str[] (name)
                nonRtServerCtrl.readUInt();

                // name
                const uint32_t nameSize(nonRtServerCtrl.readUInt());
                char name[nameSize+1];
                carla_zeroChars(name, nameSize+1);
                nonRtServerCtrl.readCustomData(name, nameSize);
            }   break;

            case kPluginBridgeNonRtServerMidiProgramData: {
                // uint/index, uint/bank, uint/program, uint/size, str[] (name)
                nonRtServerCtrl.readUInt();
                nonRtServerCtrl.readUInt();
                nonRtServerCtrl.readUInt();

                // name
                const uint32_t nameSize(nonRtServerCtrl.readUInt());
                char name[nameSize+1];
                carla_zeroChars(name, nameSize+1);
                nonRtServerCtrl.readCustomData(name, nameSize);
            }   break;

            case kPluginBridgeNonRtServerSetCustomData: {
                // uint/size, str[], uint/size, str[], uint/size, str[]

                // type
                const uint32_t typeSize = nonRtServerCtrl.readUInt();
                char type[typeSize+1];
                carla_zeroChars(type, typeSize+1);
                nonRtServerCtrl.readCustomData(type, typeSize);

                // key
                const uint32_t keySize = nonRtServerCtrl.readUInt();
                char key[keySize+1];
                carla_zeroChars(key, keySize+1);
                nonRtServerCtrl.readCustomData(key, keySize);

                // value
                const uint32_t valueSize = nonRtServerCtrl.readUInt();

                // special case for big values
//                 if (valueSize > 16384)
//                 {
//                     const uint32_t bigValueFilePathSize = nonRtServerCtrl.readUInt();
//                     char bigValueFilePath[bigValueFilePathSize+1];
//                     carla_zeroChars(bigValueFilePath, bigValueFilePathSize+1);
//                     nonRtServerCtrl.readCustomData(bigValueFilePath, bigValueFilePathSize);
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
                        nonRtServerCtrl.readCustomData(value, valueSize);

//                     CarlaPlugin::setCustomData(type, key, value, false);
//                 }

            }   break;

            case kPluginBridgeNonRtServerSetChunkDataFile: {
                // uint/size, str[] (filename)

                // chunkFilePath
                const uint32_t chunkFilePathSize = nonRtServerCtrl.readUInt();
                char chunkFilePath[chunkFilePathSize+1];
                carla_zeroChars(chunkFilePath, chunkFilePathSize+1);
                nonRtServerCtrl.readCustomData(chunkFilePath, chunkFilePathSize);

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
                nonRtServerCtrl.readUInt();
// #ifndef BUILD_BRIDGE
//                 if (! fInitiated)
//                     pData->latency.recreateBuffers(std::max(fInfo.aIns, fInfo.aOuts), fLatency);
// #endif
                break;

            case kPluginBridgeNonRtServerSetParameterText: {
                const int32_t index = nonRtServerCtrl.readInt();

                const uint32_t textSize(nonRtServerCtrl.readUInt());
                char text[textSize+1];
                carla_zeroChars(text, textSize+1);
                nonRtServerCtrl.readCustomData(text, textSize);

//                 fReceivingParamText.setReceivedData(index, text, textSize);
            }   break;

            case kPluginBridgeNonRtServerReady:
                ready = true;
                break;

            case kPluginBridgeNonRtServerSaved:
//                 fSaved = true;
                break;

            case kPluginBridgeNonRtServerRespEmbedUI:
//                 fPendingEmbedCustomUI =
                nonRtServerCtrl.readULong();
                break;

            case kPluginBridgeNonRtServerResizeEmbedUI: {
                nonRtServerCtrl.readUInt();
                nonRtServerCtrl.readUInt();
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
                const uint32_t errorSize(nonRtServerCtrl.readUInt());
                char error[errorSize+1];
                carla_zeroChars(error, errorSize+1);
                nonRtServerCtrl.readCustomData(error, errorSize);

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
    } CARLA_SAFE_EXCEPTION("handleNonRtData");

    return true;
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

void carla_bridge::set_value(uint index, float value)
{
    CARLA_SAFE_ASSERT_UINT2_RETURN(index < paramCount, index, paramCount,);

    paramDetails[index].value = value;

    {
        const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

        nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientSetParameterValue);
        nonRtClientCtrl.writeUInt(index);
        nonRtClientCtrl.writeFloat(value);
        nonRtClientCtrl.commitWrite();

        nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientUiParameterChange);
        nonRtClientCtrl.writeUInt(index);
        nonRtClientCtrl.writeFloat(value);
        nonRtClientCtrl.commitWrite();

        nonRtClientCtrl.waitIfDataIsReachingLimit();
    }
}

void carla_bridge::show_ui()
{
    if (isRunning())
    {
        const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

        nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientShowUI);
        nonRtClientCtrl.commitWrite();
    }
}

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

void carla_bridge::process(float *buffers[2], uint32_t frames)
{
    if (!isRunning())
        return;

    rtClientCtrl.data->timeInfo.usecs = os_gettime_ns() / 1000;

    for (uint32_t c=0; c < 2; ++c)
        carla_copyFloats(audiopool.data + (c * bufferSize), buffers[c], frames);

    {
        rtClientCtrl.writeOpcode(kPluginBridgeRtClientProcess);
        rtClientCtrl.writeUInt(frames);
        rtClientCtrl.commitWrite();
    }

    wait("process", 1000);
}
