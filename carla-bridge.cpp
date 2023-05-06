/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-bridge.hpp"
#include "common.h"

#include <obs-module.h>
#include <util/platform.h>

#include "CarlaBackendUtils.hpp"
#include "CarlaBase64Utils.hpp"

// #include "water/files/File.h"
// #include "water/misc/Time.h"
// #include "water/streams/MemoryOutputStream.h"

#include <ctime>

struct BridgeTextReader {
	char* text = nullptr;

	BridgeTextReader(BridgeNonRtServerControl& nonRtServerCtrl)
	{
		const uint32_t size = nonRtServerCtrl.readUInt();
		CARLA_SAFE_ASSERT_RETURN(size != 0,);

		text = new char[size + 1];
		nonRtServerCtrl.readCustomData(text, size);
		text[size] = '\0';
	}

	BridgeTextReader(BridgeNonRtServerControl& nonRtServerCtrl, const uint32_t size)
	{
		text = new char[size + 1];

		if (size != 0)
			nonRtServerCtrl.readCustomData(text, size);

		text[size] = '\0';
	}

	~BridgeTextReader() noexcept
	{
		delete[] text;
	}
};

// ----------------------------------------------------------------------------

/* TODO
static void stopProcess(water::ChildProcess *const process)
{
	// we only get here if bridge crashed or thread asked to exit
	if (process->isRunning()) {
		process->waitForProcessToFinish(2000);

		if (process->isRunning()) {
			carla_stdout(
				"CarlaPluginBridgeThread::run() - bridge refused to close, force kill now");
			process->kill();
		} else {
			carla_stdout(
				"CarlaPluginBridgeThread::run() - bridge auto-closed successfully");
		}
	} else {
		// forced quit, may have crashed
		if (process->getExitCodeAndClearPID() != 0) {
			carla_stderr(
				"CarlaPluginBridgeThread::run() - bridge crashed");

			CarlaString errorString(
				"Plugin has crashed!\n"
				"Saving now will lose its current settings.\n"
				"Please remove this plugin, and not rely on it from this point.");
		}
	}
}
*/

// ----------------------------------------------------------------------------

bool carla_bridge::init(uint32_t maxBufferSize, double sampleRate)
{
	std::srand(static_cast<uint>(std::time(nullptr)));

	if (!audiopool.initializeServer()) {
		carla_stderr("Failed to initialize shared memory audio pool");
		return false;
	}

	audiopool.resize(maxBufferSize, MAX_AV_PLANES, MAX_AV_PLANES);

	if (!rtClientCtrl.initializeServer()) {
		carla_stderr("Failed to initialize RT client control");
		goto fail1;
	}

	if (!nonRtClientCtrl.initializeServer()) {
		carla_stderr("Failed to initialize Non-RT client control");
		goto fail2;
	}

	if (!nonRtServerCtrl.initializeServer()) {
		carla_stderr("Failed to initialize Non-RT server control");
		goto fail3;
	}

	rtClientCtrl.data->procFlags = 0;
	carla_zeroStruct(rtClientCtrl.data->timeInfo);
	carla_zeroBytes(rtClientCtrl.data->midiOut,
			kBridgeRtClientDataMidiOutSize);

	rtClientCtrl.clearData();
	nonRtClientCtrl.clearData();
	nonRtServerCtrl.clearData();

	nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientVersion);
	nonRtClientCtrl.writeUInt(CARLA_PLUGIN_BRIDGE_API_VERSION_CURRENT);

	nonRtClientCtrl.writeUInt(
		static_cast<uint32_t>(sizeof(BridgeRtClientData)));
	nonRtClientCtrl.writeUInt(
		static_cast<uint32_t>(sizeof(BridgeNonRtClientData)));
	nonRtClientCtrl.writeUInt(
		static_cast<uint32_t>(sizeof(BridgeNonRtServerData)));

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
	std::strncpy(shmIdsStr + 6 * 0,
		     &audiopool.filename[audiopool.filename.length() - 6], 6);
	std::strncpy(shmIdsStr + 6 * 1,
		     &rtClientCtrl.filename[rtClientCtrl.filename.length() - 6],
		     6);
	std::strncpy(
		shmIdsStr + 6 * 2,
		&nonRtClientCtrl.filename[nonRtClientCtrl.filename.length() - 6],
		6);
	std::strncpy(
		shmIdsStr + 6 * 3,
		&nonRtServerCtrl.filename[nonRtServerCtrl.filename.length() - 6],
		6);

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

	/* TODO
	if (childprocess != nullptr) {
		if (childprocess->isRunning()) {
			nonRtClientCtrl.writeOpcode(
				kPluginBridgeNonRtClientQuit);
			nonRtClientCtrl.commitWrite();

			rtClientCtrl.writeOpcode(kPluginBridgeRtClientQuit);
			rtClientCtrl.commitWrite();

			if (!timedOut)
				wait("stopping", 3000);
		}

		stopProcess(childprocess);
		childprocess = nullptr;
	}
	*/

	nonRtServerCtrl.clear();
	nonRtClientCtrl.clear();
	rtClientCtrl.clear();
	audiopool.clear();
	info.clear();
}

bool carla_bridge::start(const PluginType type,
			 const char *const binaryArchName,
			 const char *const bridgeBinary, const char *label,
			 const char *filename, const int64_t uniqueId)
{
	UNUSED_PARAMETER(binaryArchName);

	/* TODO
	if (childprocess == nullptr) {
		childprocess = new water::ChildProcess();
	} else if (childprocess->isRunning()) {
		carla_stderr(
			"CarlaPluginBridgeThread::run() - already running");
	}

	char strBuf[STR_MAX + 1];
	strBuf[STR_MAX] = '\0';

	// setup binary arch
	water::ChildProcess::Type childType;
#ifdef CARLA_OS_MAC
	if (std::strcmp(binaryArchName, "arm64") == 0)
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
	*/

	bool started;

	{
		const CarlaScopedEnvVar sev("ENGINE_BRIDGE_SHM_IDS", shmIdsStr);

		carla_stdout(
			"Starting plugin bridge, command is:\n%s \"%s\" \"%s\" \"%s\" " P_INT64,
			bridgeBinary, getPluginTypeAsString(type), filename,
			label, uniqueId);

		started = false;
		/* TODO
		childprocess->start(arguments, childType);
		*/
	}

	if (!started) {
		carla_stdout("failed!");
		/* TODO
		childprocess = nullptr;
		*/
		return false;
	}

	ready = false;
	timedOut = false;

	while (isRunning() && idle() && !ready)
		carla_msleep(5);

	if (ready && activated) {
		nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientActivate);
		nonRtClientCtrl.commitWrite();
	}

	info.ptype = type;
	info.filename = filename;
	info.label = label;

	return ready;
}

bool carla_bridge::isRunning() const
{
	return false;
	/* TODO
	return childprocess != nullptr && childprocess->isRunning();
	*/
}

bool carla_bridge::isReady() const noexcept
{
	return ready;
}

bool carla_bridge::idle()
{
	/* TODO
	if (childprocess == nullptr)
		return false;

	if (childprocess->isRunning()) {
		const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

		nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientPing);
		nonRtClientCtrl.commitWrite();
	} else {
		//         fTimedError = true;
		printf("bridge closed by itself!\n");
		activated = false;
		timedOut = true;
		cleanup();
		return false;
	}
	*/

	//         if (priv->loaded && fTimedOut && pData->active)
	//             setActive(false, true, true);

	try {
		readMessages();
	}
	CARLA_SAFE_EXCEPTION("readMessages");

	return true;
}

bool carla_bridge::wait(const char *const action, const uint msecs)
{
	CARLA_SAFE_ASSERT_RETURN(!timedOut, false);
	//         CARLA_SAFE_ASSERT_RETURN(! fTimedError,);

	if (rtClientCtrl.waitForClient(msecs))
		return true;

	timedOut = true;
	carla_stderr2("waitForClient(%s) timed out", action);
	return false;
}

// ----------------------------------------------------------------------------

void carla_bridge::set_value(uint index, float value)
{
	CARLA_SAFE_ASSERT_UINT2_RETURN(index < paramCount, index, paramCount, );

	paramDetails[index].value = value;

	{
		const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

		nonRtClientCtrl.writeOpcode(
			kPluginBridgeNonRtClientSetParameterValue);
		nonRtClientCtrl.writeUInt(index);
		nonRtClientCtrl.writeFloat(value);
		nonRtClientCtrl.commitWrite();

		nonRtClientCtrl.writeOpcode(
			kPluginBridgeNonRtClientUiParameterChange);
		nonRtClientCtrl.writeUInt(index);
		nonRtClientCtrl.writeFloat(value);
		nonRtClientCtrl.commitWrite();

		nonRtClientCtrl.waitIfDataIsReachingLimit();
	}
}

void carla_bridge::show_ui()
{
	if (isRunning()) {
		const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

		nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientShowUI);
		nonRtClientCtrl.commitWrite();
	}
}

void carla_bridge::activate()
{
	assert(!activated);
	activated = true;
	timedOut = false;

	if (isRunning()) {
		{
			const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

			nonRtClientCtrl.writeOpcode(
				kPluginBridgeNonRtClientActivate);
			nonRtClientCtrl.commitWrite();
		}

		try {
			wait("activate", 2000);
		}
		CARLA_SAFE_EXCEPTION("activate - waitForClient");
	}
}

void carla_bridge::deactivate()
{
	assert(activated);
	activated = false;
	// timedOut = false;

	if (isRunning()) {
		{
			const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

			nonRtClientCtrl.writeOpcode(
				kPluginBridgeNonRtClientDeactivate);
			nonRtClientCtrl.commitWrite();
		}

		try {
			wait("deactivate", 2000);
		}
		CARLA_SAFE_EXCEPTION("deactivate - waitForClient");
	}
}

void carla_bridge::process(float *buffers[2], uint32_t frames)
{
	CARLA_SAFE_ASSERT_RETURN(activated, );
	CARLA_SAFE_ASSERT_RETURN(ready, );

	if (!isRunning())
		return;

	rtClientCtrl.data->timeInfo.usecs = os_gettime_ns() / 1000;

	for (uint32_t c = 0; c < 2; ++c)
		carla_copyFloats(audiopool.data + (c * bufferSize), buffers[c],
				 frames);

	{
		rtClientCtrl.writeOpcode(kPluginBridgeRtClientProcess);
		rtClientCtrl.writeUInt(frames);
		rtClientCtrl.commitWrite();
	}

	if (wait("process", 1000)) {
		for (uint32_t c = 0; c < 2; ++c)
			carla_copyFloats(buffers[c],
					 audiopool.data + ((c + info.numAudioIns) *
							   bufferSize),
					 frames);
	}
}

void carla_bridge::load_chunk()
{
	const CarlaString dataBase64(CarlaString::asBase64(info.chunk.data(),
							   info.chunk.size()));
	CARLA_SAFE_ASSERT_RETURN(dataBase64.length() > 0,);

	/* TODO
	water::String filePath(water::File::getSpecialLocation(water::File::tempDirectory).getFullPathName());

	filePath += CARLA_OS_SEP_STR ".CarlaChunk_";
	filePath += audiopool.getFilenameSuffix();

	if (water::File(filePath).replaceWithText(dataBase64.buffer()))
	{
		const uint32_t ulength = static_cast<uint32_t>(filePath.length());

		const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

		nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientSetChunkDataFile);
		nonRtClientCtrl.writeUInt(ulength);
		nonRtClientCtrl.writeCustomData(filePath.toRawUTF8(), ulength);
		nonRtClientCtrl.commitWrite();
	}
	*/
}

void carla_bridge::save_and_wait()
{
	saved = false;

	{
		const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

		// deactivate bridge client-side ping check
		// some plugins block during load
		nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientPingOnOff);
		nonRtClientCtrl.writeBool(false);
		nonRtClientCtrl.commitWrite();

		nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientPrepareForSave);
		nonRtClientCtrl.commitWrite();
	}

	while (isRunning() && idle() && !saved)
		carla_msleep(5);

	if (isRunning())
	{
		const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

		nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientPingOnOff);
		nonRtClientCtrl.writeBool(true);
		nonRtClientCtrl.commitWrite();
	}
}

// ----------------------------------------------------------------------------

void carla_bridge::readMessages()
{
	while (nonRtServerCtrl.isDataAvailableForReading()) {
		const PluginBridgeNonRtServerOpcode opcode =
			nonRtServerCtrl.readOpcode();

		// #ifdef DEBUG
		if (opcode != kPluginBridgeNonRtServerPong &&
			opcode != kPluginBridgeNonRtServerParameterValue2) {
			carla_stdout(
				"CarlaPluginBridge::handleNonRtData() - got opcode: %s",
				PluginBridgeNonRtServerOpcode2str(
					opcode));
		}
		// #endif

		switch (opcode) {
		case kPluginBridgeNonRtServerNull:
		case kPluginBridgeNonRtServerPong:
			break;

		// uint/version
		case kPluginBridgeNonRtServerVersion:
			nonRtServerCtrl.readUInt();
			break;

		// uint/category, uint/hints, uint/optionsAvailable, uint/optionsEnabled, long/uniqueId
		case kPluginBridgeNonRtServerPluginInfo1: {
			info.clear();

			const uint32_t category =
				nonRtServerCtrl.readUInt();
			info.hints = nonRtServerCtrl.readUInt() | PLUGIN_IS_BRIDGE;
			const uint32_t optionAv =
				nonRtServerCtrl.readUInt();
			info.options = nonRtServerCtrl.readUInt();
			const int64_t uniqueId =
				nonRtServerCtrl.readLong();

			if (info.uniqueId != 0) {
				CARLA_SAFE_ASSERT_INT2(info.uniqueId == uniqueId, info.uniqueId, uniqueId);
			}

			#ifdef HAVE_X11
			// if (fBridgeVersion < 9 || fBinaryType == BINARY_WIN32 || fBinaryType == BINARY_WIN64)
			#endif
			{
				info.hints &= ~PLUGIN_HAS_CUSTOM_EMBED_UI;
			}
		} break;

		// uint/size, str[] (realName), uint/size, str[] (label), uint/size, str[] (maker), uint/size, str[] (copyright)
		case kPluginBridgeNonRtServerPluginInfo2: {
			// realName
			if (const uint32_t size = nonRtServerCtrl.readUInt())
				nonRtServerCtrl.skipRead(size);

			// label
			if (const uint32_t size = nonRtServerCtrl.readUInt())
				nonRtServerCtrl.skipRead(size);

			// maker
			if (const uint32_t size = nonRtServerCtrl.readUInt())
				nonRtServerCtrl.skipRead(size);

			// copyright
			if (const uint32_t size = nonRtServerCtrl.readUInt())
				nonRtServerCtrl.skipRead(size);
		} break;

		// uint/ins, uint/outs
		case kPluginBridgeNonRtServerAudioCount:
			nonRtServerCtrl.readUInt();
			nonRtServerCtrl.readUInt();
			break;

		// uint/ins, uint/outs
		case kPluginBridgeNonRtServerMidiCount:
			nonRtServerCtrl.readUInt();
			nonRtServerCtrl.readUInt();
			break;

		// uint/ins, uint/outs
		case kPluginBridgeNonRtServerCvCount:
			nonRtServerCtrl.readUInt();
			nonRtServerCtrl.readUInt();
			break;

		// uint/count
		case kPluginBridgeNonRtServerParameterCount: {
			paramCount = nonRtServerCtrl.readUInt();

			delete[] paramDetails;

			if (paramCount != 0)
				paramDetails =
					new carla_param_data[paramCount];
			else
				paramDetails = nullptr;
		} break;

		// uint/count
		case kPluginBridgeNonRtServerProgramCount:
			nonRtServerCtrl.readUInt();
			break;

		// uint/count
		case kPluginBridgeNonRtServerMidiProgramCount:
			nonRtServerCtrl.readUInt();
			break;

		// byte/type, uint/index, uint/size, str[] (name)
		case kPluginBridgeNonRtServerPortName: {
			nonRtServerCtrl.readByte();
			nonRtServerCtrl.readUInt();

			// name
			if (const uint32_t size = nonRtServerCtrl.readUInt())
				nonRtServerCtrl.skipRead(size);

		} break;

		// uint/index, int/rindex, uint/type, uint/hints, int/cc
		case kPluginBridgeNonRtServerParameterData1: {
			const uint32_t index =
				nonRtServerCtrl.readUInt();
			const int32_t rindex =
				nonRtServerCtrl.readInt();
			const uint32_t type =
				nonRtServerCtrl.readUInt();
			const uint32_t hints =
				nonRtServerCtrl.readUInt();
			const int16_t ctrl =
				nonRtServerCtrl.readShort();

			CARLA_SAFE_ASSERT_INT_BREAK(
				ctrl >= CONTROL_INDEX_NONE &&
					ctrl <= CONTROL_INDEX_MAX_ALLOWED,
				ctrl);
			CARLA_SAFE_ASSERT_UINT2_BREAK(
				index < paramCount, index, paramCount);

			if (type != PARAMETER_INPUT)
				break;
			if ((hints & PARAMETER_IS_ENABLED) == 0)
				break;
			if (hints & (PARAMETER_IS_READ_ONLY |
					PARAMETER_IS_NOT_SAVED))
				break;

			paramDetails[index].hints = hints;
		} break;

		// uint/index, uint/size, str[] (name), uint/size, str[] (unit)
		case kPluginBridgeNonRtServerParameterData2: {
			const uint32_t index =
				nonRtServerCtrl.readUInt();

			// name
			const BridgeTextReader name(nonRtServerCtrl);

			// symbol
			const BridgeTextReader symbol(nonRtServerCtrl);

			// unit
			const BridgeTextReader unit(nonRtServerCtrl);

			CARLA_SAFE_ASSERT_UINT2_BREAK(
				index < paramCount, index, paramCount);

			if (paramDetails[index].hints &
				PARAMETER_IS_ENABLED) {
				paramDetails[index].name = name.text;
				paramDetails[index].symbol = symbol.text;
				paramDetails[index].unit = unit.text;
			}
		} break;

		// uint/index, float/def, float/min, float/max, float/step, float/stepSmall, float/stepLarge
		case kPluginBridgeNonRtServerParameterRanges: {
			const uint32_t index =
				nonRtServerCtrl.readUInt();
			const float def = nonRtServerCtrl.readFloat();
			const float min = nonRtServerCtrl.readFloat();
			const float max = nonRtServerCtrl.readFloat();
			const float step = nonRtServerCtrl.readFloat();
			nonRtServerCtrl.readFloat();
			nonRtServerCtrl.readFloat();

			CARLA_SAFE_ASSERT_BREAK(min < max);
			CARLA_SAFE_ASSERT_BREAK(def >= min);
			CARLA_SAFE_ASSERT_BREAK(def <= max);
			CARLA_SAFE_ASSERT_UINT2_BREAK(
				index < paramCount, index, paramCount);

			if (paramDetails[index].hints &
				PARAMETER_IS_ENABLED) {
				paramDetails[index].def =
					paramDetails[index].value = def;
				paramDetails[index].min = min;
				paramDetails[index].max = max;
				paramDetails[index].step = step;
			}
		} break;

		// uint/index, float/value
		case kPluginBridgeNonRtServerParameterValue: {
			const uint32_t index =
				nonRtServerCtrl.readUInt();
			const float value = nonRtServerCtrl.readFloat();

			if (index < paramCount) {
				const float fixedValue =
					carla_fixedValue(
						paramDetails[index].min,
						paramDetails[index].max,
						value);

				if (carla_isNotEqual(
						paramDetails[index].value,
						fixedValue)) {
					paramDetails[index].value =
						fixedValue;

					if (callback != nullptr) {
						// skip parameters that we do not show
						if ((paramDetails[index]
								.hints &
							PARAMETER_IS_ENABLED) ==
							0)
							break;

						callback->bridge_parameter_changed(
							index,
							fixedValue);
					}
				}
			}
		} break;

		// uint/index, float/value
		case kPluginBridgeNonRtServerParameterValue2: {
			const uint32_t index =
				nonRtServerCtrl.readUInt();
			const float value = nonRtServerCtrl.readFloat();

			if (index < paramCount) {
				const float fixedValue =
					carla_fixedValue(
						paramDetails[index].min,
						paramDetails[index].max,
						value);
				paramDetails[index].value = fixedValue;
			}
		} break;

		// uint/index, bool/touch
		case kPluginBridgeNonRtServerParameterTouch:
			nonRtServerCtrl.readUInt();
			nonRtServerCtrl.readBool();
			break;

		// uint/index, float/value
		case kPluginBridgeNonRtServerDefaultValue: {
			const uint32_t index =
				nonRtServerCtrl.readUInt();
			const float value = nonRtServerCtrl.readFloat();

			if (index < paramCount)
				paramDetails[index].def = value;
		} break;

		// int/index
		case kPluginBridgeNonRtServerCurrentProgram:
			nonRtServerCtrl.readInt();
			break;

		// int/index
		case kPluginBridgeNonRtServerCurrentMidiProgram:
			nonRtServerCtrl.readInt();
			break;

		// uint/index, uint/size, str[] (name)
		case kPluginBridgeNonRtServerProgramName: {
			nonRtServerCtrl.readUInt();

			// name
			if (const uint32_t size = nonRtServerCtrl.readUInt())
				nonRtServerCtrl.skipRead(size);
		} break;

		// uint/index, uint/bank, uint/program, uint/size, str[] (name)
		case kPluginBridgeNonRtServerMidiProgramData: {
			nonRtServerCtrl.readUInt();
			nonRtServerCtrl.readUInt();
			nonRtServerCtrl.readUInt();

			// name
			if (const uint32_t size = nonRtServerCtrl.readUInt())
				nonRtServerCtrl.skipRead(size);
		} break;

		// uint/size, str[], uint/size, str[], uint/size, str[]
		case kPluginBridgeNonRtServerSetCustomData: {
			// type
			const BridgeTextReader type(nonRtServerCtrl);

			// key
			const BridgeTextReader key(nonRtServerCtrl);

			// value
			const uint32_t valueSize =
				nonRtServerCtrl.readUInt();

			// special case for big values
			if (valueSize > 16384)
			{
				const BridgeTextReader bigValueFilePath(nonRtServerCtrl, valueSize);

// 				String realBigValueFilePath(bigValueFilePath);

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

// 				const File bigValueFile(realBigValueFilePath);
// 				CARLA_SAFE_ASSERT_BREAK(bigValueFile.existsAsFile());
//
// 				CarlaPlugin::setCustomData(type, key, bigValueFile.loadFileAsString().toRawUTF8(), false);
//
// 				bigValueFile.deleteFile();
			}
			else
			{
				const BridgeTextReader value(nonRtServerCtrl, valueSize);

// 				CarlaPlugin::setCustomData(type, key, value, false);
			}

		} break;

		// uint/size, str[] (filename, base64 content)
		case kPluginBridgeNonRtServerSetChunkDataFile: {
			// chunkFilePath
			const BridgeTextReader chunkFilePath(nonRtServerCtrl);

			/* TODO
			water::String realChunkFilePath(chunkFilePath);

// #ifndef CARLA_OS_WIN
// 			// Using Wine, fix temp dir
// 			if (fBinaryType == BINARY_WIN32 || fBinaryType == BINARY_WIN64)
// 			{
// 				const StringArray driveLetterSplit(StringArray::fromTokens(realChunkFilePath, ":/", ""));
// 				carla_stdout("chunk save path BEFORE => %s", realChunkFilePath.toRawUTF8());
//
// 				realChunkFilePath  = fWinePrefix;
// 				realChunkFilePath += "/drive_";
// 				realChunkFilePath += driveLetterSplit[0].toLowerCase();
// 				realChunkFilePath += driveLetterSplit[1];
//
// 				realChunkFilePath  = realChunkFilePath.replace("\\", "/");
// 				carla_stdout("chunk save path AFTER => %s", realChunkFilePath.toRawUTF8());
// 			}
// #endif

			const water::File chunkFile(realChunkFilePath);
			CARLA_SAFE_ASSERT_BREAK(chunkFile.existsAsFile());

			info.chunk = carla_getChunkFromBase64String(chunkFile.loadFileAsString().toRawUTF8());
			chunkFile.deleteFile();
			*/
		} break;

		// uint/latency
		case kPluginBridgeNonRtServerSetLatency:
			//                 fLatency =
			nonRtServerCtrl.readUInt();
			// #ifndef BUILD_BRIDGE
			//                 if (! fInitiated)
			//                     pData->latency.recreateBuffers(std::max(fInfo.aIns, fInfo.aOuts), fLatency);
			// #endif
			break;

		case kPluginBridgeNonRtServerSetParameterText: {
			nonRtServerCtrl.readInt();

			if (const uint32_t size = nonRtServerCtrl.readUInt())
				nonRtServerCtrl.skipRead(size);

			// fReceivingParamText.setReceivedData(index, text, textSize);
		} break;

		case kPluginBridgeNonRtServerReady:
			ready = true;
			break;

		case kPluginBridgeNonRtServerSaved:
			saved = true;
			break;

		// ulong/window-id
		case kPluginBridgeNonRtServerRespEmbedUI:
			nonRtServerCtrl.readULong();
			break;

		// uint/width, uint/height
		case kPluginBridgeNonRtServerResizeEmbedUI:
			nonRtServerCtrl.readUInt();
			nonRtServerCtrl.readUInt();
			break;

		case kPluginBridgeNonRtServerUiClosed:
			// #ifndef BUILD_BRIDGE_ALTERNATIVE_ARCH
			//                 pData->transientTryCounter = 0;
			// #endif
			//                 pData->engine->callback(true, true, ENGINE_CALLBACK_UI_STATE_CHANGED, pData->id,
			//                                         0, 0, 0, 0.0f, nullptr);
			break;

		// uint/size, str[]
		case kPluginBridgeNonRtServerError: {
			const BridgeTextReader error(nonRtServerCtrl);

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
		} break;
		}
	}
}
