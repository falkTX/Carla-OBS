/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-bridge.hpp"
#include "carla-wrapper.h"
#include "qtutils.h"
#include <util/platform.h>

#include "CarlaBackendUtils.hpp"
#include "CarlaJuceUtils.hpp"
#include "CarlaShmUtils.hpp"

#include "jackbridge/JackBridge.hpp"

#include "CarlaFrontend.h"

#include <vector>

// generates a warning if this is defined as anything else
#define CARLA_API

// --------------------------------------------------------------------------------------------------------------------
// private data methods

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

struct carla_priv {
    obs_source_t *source = nullptr;
    uint32_t bufferSize = 0;
    double sampleRate = 0;
    bool loaded = false;

    // cached parameter info
    uint32_t paramCount = 0;
    struct carla_param_data* paramDetails = nullptr;

    // update properties when timeout is reached, 0 means do nothing
    uint64_t update_requested = 0;

    carla_bridge bridge;

    ~carla_priv()
    {
        delete[] paramDetails;
    }

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

                fInfo.name  = realName;
                fInfo.label = label;
                fInfo.maker = maker;
                fInfo.copyright = copyright;
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
                paramCount = bridge.nonRtServerCtrl.readUInt();

                delete[] paramDetails;

                if (paramCount != 0)
                    paramDetails = new carla_param_data[paramCount];
                else
                    paramDetails = nullptr;

            }   break;

            case kPluginBridgeNonRtServerProgramCount: {
                // uint/count
                bridge.nonRtServerCtrl.readUInt();
            }   break;

            case kPluginBridgeNonRtServerMidiProgramCount: {
                // uint/count
                bridge.nonRtServerCtrl.readUInt();
            }   break;

            case kPluginBridgeNonRtServerPortName: {
                // byte/type, uint/index, uint/size, str[] (name)
                bridge.nonRtServerCtrl.readByte();
                bridge.nonRtServerCtrl.readUInt();

                // name
                const uint32_t nameSize(bridge.nonRtServerCtrl.readUInt());
                char* const name = new char[nameSize+1];
                carla_zeroChars(name, nameSize+1);
                bridge.nonRtServerCtrl.readCustomData(name, nameSize);

            }   break;

            case kPluginBridgeNonRtServerParameterData1: {
                // uint/index, int/rindex, uint/type, uint/hints, int/cc
                const uint32_t index  = bridge.nonRtServerCtrl.readUInt();
                const  int32_t rindex = bridge.nonRtServerCtrl.readInt();
                const uint32_t type   = bridge.nonRtServerCtrl.readUInt();
                const uint32_t hints  = bridge.nonRtServerCtrl.readUInt();
                const  int16_t ctrl   = bridge.nonRtServerCtrl.readShort();

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
                const uint32_t index = bridge.nonRtServerCtrl.readUInt();
                const float def      = bridge.nonRtServerCtrl.readFloat();
                const float min      = bridge.nonRtServerCtrl.readFloat();
                const float max      = bridge.nonRtServerCtrl.readFloat();
                const float step      = bridge.nonRtServerCtrl.readFloat();
                bridge.nonRtServerCtrl.readFloat();
                bridge.nonRtServerCtrl.readFloat();

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
                const uint32_t index = bridge.nonRtServerCtrl.readUInt();
                const float    value = bridge.nonRtServerCtrl.readFloat();

                if (index < paramCount)
                {
                    const float fixedValue = carla_fixedValue(paramDetails[index].min, paramDetails[index].max, value);

                    if (carla_isNotEqual(paramDetails[index].value, fixedValue))
                    {
                        paramDetails[index].value = fixedValue;

                        // skip parameters that we do not show
                        if ((paramDetails[index].hints & PARAMETER_IS_ENABLED) == 0)
                            break;

                        char pname[PARAM_NAME_SIZE] = PARAM_NAME_INIT;
                        param_index_to_name(index, pname);

                        // obs_source_t *source = priv->source;
                        obs_data_t *settings = obs_source_get_settings(source);

                        /**/ if (paramDetails[index].hints & PARAMETER_IS_BOOLEAN)
                            obs_data_set_bool(settings, pname, value > 0.5f ? 1.f : 0.f);
                        else if (paramDetails[index].hints & PARAMETER_IS_INTEGER)
                            obs_data_set_int(settings, pname, value);
                        else
                            obs_data_set_double(settings, pname, value);

                        obs_data_release(settings);

                        update_requested = os_gettime_ns();
                    }
                }
            }   break;

            case kPluginBridgeNonRtServerParameterValue2: {
                // uint/index, float/value
                const uint32_t index = bridge.nonRtServerCtrl.readUInt();
                const float    value = bridge.nonRtServerCtrl.readFloat();

                if (index < paramCount)
                {
                    const float fixedValue = carla_fixedValue(paramDetails[index].min, paramDetails[index].max, value);
                    paramDetails[index].value = fixedValue;
                }
            }   break;

            case kPluginBridgeNonRtServerParameterTouch: {
                // uint/index, bool/touch
                bridge.nonRtServerCtrl.readUInt();
                bridge.nonRtServerCtrl.readBool();
            }   break;

            case kPluginBridgeNonRtServerDefaultValue: {
                // uint/index, float/value
                const uint32_t index = bridge.nonRtServerCtrl.readUInt();
                const float    value = bridge.nonRtServerCtrl.readFloat();

                if (index < paramCount)
                    paramDetails[index].def = value;
            }   break;

            case kPluginBridgeNonRtServerCurrentProgram: {
                // int/index
                bridge.nonRtServerCtrl.readInt();
            }   break;

            case kPluginBridgeNonRtServerCurrentMidiProgram: {
                // int/index
                bridge.nonRtServerCtrl.readInt();
            }   break;

            case kPluginBridgeNonRtServerProgramName: {
                // uint/index, uint/size, str[] (name)
                bridge.nonRtServerCtrl.readUInt();

                // name
                const uint32_t nameSize(bridge.nonRtServerCtrl.readUInt());
                char name[nameSize+1];
                carla_zeroChars(name, nameSize+1);
                bridge.nonRtServerCtrl.readCustomData(name, nameSize);
            }   break;

            case kPluginBridgeNonRtServerMidiProgramData: {
                // uint/index, uint/bank, uint/program, uint/size, str[] (name)
                bridge.nonRtServerCtrl.readUInt();
                bridge.nonRtServerCtrl.readUInt();
                bridge.nonRtServerCtrl.readUInt();

                // name
                const uint32_t nameSize(bridge.nonRtServerCtrl.readUInt());
                char name[nameSize+1];
                carla_zeroChars(name, nameSize+1);
                bridge.nonRtServerCtrl.readCustomData(name, nameSize);
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
                bridge.nonRtServerCtrl.readUInt();
                bridge.nonRtServerCtrl.readUInt();
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

    return priv;

fail1:
    delete priv;
    return nullptr;
}

void carla_priv_destroy(struct carla_priv *priv)
{
    priv->bridge.cleanup();
    delete priv;
}

// --------------------------------------------------------------------------------------------------------------------

void carla_priv_activate(struct carla_priv *priv)
{
    priv->bridge.activate();
}

void carla_priv_deactivate(struct carla_priv *priv)
{
    priv->bridge.deactivate();
}

void carla_priv_process_audio(struct carla_priv *priv, float *buffers[2], uint32_t frames)
{
    if (!priv->bridge.isRunning())
        return;

    for (uint32_t c=0; c < 2; ++c)
        carla_copyFloats(priv->bridge.audiopool.data + (c * priv->bufferSize), buffers[c], frames);

    {
        priv->bridge.rtClientCtrl.writeOpcode(kPluginBridgeRtClientProcess);
        priv->bridge.rtClientCtrl.writeUInt(frames);
        priv->bridge.rtClientCtrl.commitWrite();
    }

    priv->bridge.wait("process", 1000);

}

void carla_priv_idle(struct carla_priv *priv)
{
    if (priv->bridge.isRunning())
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

    if (priv->update_requested != 0)
    {
        const uint64_t now = os_gettime_ns();

        // request in the future?
        if (now < priv->update_requested)
        {
            priv->update_requested = now;
            return;
        }

        if (now - priv->update_requested >= 100000000ULL) // 100ms
        {
            priv->update_requested = 0;

            signal_handler_t *sighandler = obs_source_get_signal_handler(priv->source);
            signal_handler_signal(sighandler, "update_properties", NULL);
        }
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

static bool carla_priv_param_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(props);

    struct carla_priv *priv = static_cast<struct carla_priv*>(data);

    const char *const pname = obs_property_name(property);
    if (pname == NULL)
        return false;

    const char* pname2 = pname + 1;
    while (*pname2 == '0')
        ++pname2;

    const int pindex = atoi(pname2);

    if (pindex < 0 || pindex >= (int)priv->paramCount)
        return false;

    const uint index = static_cast<uint>(pindex);

    const float min = priv->paramDetails[index].min;
    const float max = priv->paramDetails[index].max;

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

    priv->paramDetails[index].value = value;

    {
        const CarlaMutexLocker _cml(priv->bridge.nonRtClientCtrl.mutex);

        priv->bridge.nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientSetParameterValue);
        priv->bridge.nonRtClientCtrl.writeUInt(index);
        priv->bridge.nonRtClientCtrl.writeFloat(value);
        priv->bridge.nonRtClientCtrl.commitWrite();

        priv->bridge.nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientUiParameterChange);
        priv->bridge.nonRtClientCtrl.writeUInt(index);
        priv->bridge.nonRtClientCtrl.writeFloat(value);
        priv->bridge.nonRtClientCtrl.commitWrite();

        priv->bridge.nonRtClientCtrl.waitIfDataIsReachingLimit();
    }

    return false;
}

void carla_priv_readd_properties(struct carla_priv *priv, obs_properties_t *props, bool reset)
{
    obs_data_t *settings = obs_source_get_settings(priv->source);

    if (priv->bridge.isRunning())
    {
        obs_properties_add_button2(props, PROP_SHOW_GUI, obs_module_text("Show custom GUI"),
                                   carla_priv_show_gui_callback, priv);
    }

    char pname[PARAM_NAME_SIZE] = PARAM_NAME_INIT;

    for (uint32_t i=0; i < priv->paramCount; ++i)
    {
        const carla_param_data& param(priv->paramDetails[i]);

        if ((param.hints & PARAMETER_IS_ENABLED) == 0)
            continue;

        obs_property_t *prop;
        param_index_to_name(i, pname);

        if (param.hints & PARAMETER_IS_BOOLEAN)
        {
            prop = obs_properties_add_bool(props, pname, param.name);

            obs_data_set_default_bool(settings, pname, carla_isEqual(param.def, param.max));

            if (reset)
                obs_data_set_bool(settings, pname, carla_isEqual(param.value, param.max));
        }
        else if (param.hints & PARAMETER_IS_INTEGER)
        {
            prop = obs_properties_add_int_slider(props, pname, param.name,
                                                 param.min, param.max, param.step);

            obs_data_set_default_int(settings, pname, param.def);

            if (param.unit.isNotEmpty())
                obs_property_int_set_suffix(prop, param.unit);

            if (reset)
                obs_data_set_int(settings, pname, param.value);
        }
        else
        {
            prop = obs_properties_add_float_slider(props, pname, param.name,
                                                   param.min, param.max, param.step);

            obs_data_set_default_double(settings, pname, param.def);

            if (param.unit.isNotEmpty())
                obs_property_float_set_suffix(prop, param.unit);

            if (reset)
                obs_data_set_double(settings, pname, param.value);
        }

        obs_property_set_modified_callback2(prop, carla_priv_param_changed, priv);
    }

    obs_data_release(settings);
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

    priv->bridge.cleanup();
    priv->bridge.init(priv->bufferSize, priv->sampleRate);

    priv->loaded = false;

    priv->bridge.start((PluginType)plugin->type,
                       "x86_64",
                       "/usr/lib/carla/carla-bridge-native",
                       plugin->label,
                       plugin->filename,
                       plugin->uniqueId);

    for (;priv->bridge.isRunning();)
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

    if (priv->bridge.isRunning())
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
