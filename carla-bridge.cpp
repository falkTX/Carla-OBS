/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "carla-bridge.hpp"
#include "common.h"
#include "qtutils.h"

#include <obs-module.h>
#include <util/platform.h>

#include "CarlaBackendUtils.hpp"
#include "CarlaBase64Utils.hpp"

// #include "water/files/File.h"
// #include "water/misc/Time.h"
// #include "water/streams/MemoryOutputStream.h"

#include <ctime>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtWidgets/QMessageBox>

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

	CARLA_DECLARE_NON_COPYABLE(BridgeTextReader)
};

// ----------------------------------------------------------------------------

BridgeProcess::BridgeProcess()
{
	moveToThread(qApp->thread());
}

void BridgeProcess::start()
{
	setInputChannelMode(QProcess::ForwardedInputChannel);
	setProcessChannelMode(QProcess::ForwardedChannels);
	QProcess::start(QIODevice::Unbuffered | QIODevice::ReadOnly);
}

void BridgeProcess::stop()
{
	if (state() != QProcess::NotRunning) {
		terminate();
		waitForFinished(2000);

		if (state() != QProcess::NotRunning) {
			blog(LOG_INFO, "[" CARLA_MODULE_ID "] bridge refused to close, force kill now");
			kill();
		} else {
			blog(LOG_DEBUG, "[" CARLA_MODULE_ID "] bridge auto-closed successfully");
		}
	}

	deleteLater();
}

// ----------------------------------------------------------------------------

bool carla_bridge::init(uint32_t maxBufferSize, double sampleRate)
{
	std::srand(static_cast<uint>(std::time(nullptr)));

	if (!audiopool.initializeServer()) {
		blog(LOG_WARNING, "[" CARLA_MODULE_ID "] Failed to initialize shared memory audio pool");
		return false;
	}

	audiopool.resize(maxBufferSize, MAX_AV_PLANES, MAX_AV_PLANES);

	if (!rtClientCtrl.initializeServer()) {
		blog(LOG_WARNING, "[" CARLA_MODULE_ID "] Failed to initialize RT client control");
		goto fail1;
	}

	if (!nonRtClientCtrl.initializeServer()) {
		blog(LOG_WARNING, "[" CARLA_MODULE_ID "] Failed to initialize Non-RT client control");
		goto fail2;
	}

	if (!nonRtServerCtrl.initializeServer()) {
		blog(LOG_WARNING, "[" CARLA_MODULE_ID "] Failed to initialize Non-RT server control");
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
	blog(LOG_DEBUG, "[" CARLA_MODULE_ID "] init bridge with %u buffer size", bufferSize);

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

	if (childprocess != nullptr) {
		BridgeProcess *proc = childprocess;
		childprocess = nullptr;

		if (proc->state() != QProcess::NotRunning) {
			nonRtClientCtrl.writeOpcode(
				kPluginBridgeNonRtClientQuit);
			nonRtClientCtrl.commitWrite();

			rtClientCtrl.writeOpcode(kPluginBridgeRtClientQuit);
			rtClientCtrl.commitWrite();

			if (!timedOut)
				wait("stopping", 3000);
		}
		else {
			// forced quit, may have crashed
			if (proc->exitStatus() == QProcess::CrashExit) {
				blog(LOG_WARNING, "[" CARLA_MODULE_ID "] carla_bridge::cleanup() - bridge crashed");

				/*
				QMessageBox::critical(
					nullptr,
					QString::fromUtf8("Plugin crash"),
					QString::fromUtf8(
						"Plugin has crashed!\n"
						"Saving now will lose its current settings.\n"
						"Please remove this plugin, and not rely on it from this point."));
				*/
			}
		}

		QMetaObject::invokeMethod(proc, "stop");
	}

	nonRtServerCtrl.clear();
	nonRtClientCtrl.clear();
	rtClientCtrl.clear();
	audiopool.clear();
	info.clear();
	chunk.clear();
	clear_custom_data();
}

bool carla_bridge::start(const PluginType type,
			 const char *const binaryArchName,
			 const char *const bridgeBinary, const char *label,
			 const char *filename, const int64_t uniqueId)
{
	CARLA_SAFE_ASSERT_RETURN(type != PLUGIN_NONE, false);

	UNUSED_PARAMETER(binaryArchName);

	BridgeProcess *proc = new BridgeProcess();

	/* TODO
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
	*/

	// do not use null strings for label and filename
	if (label == nullptr || label[0] == '\0')
		label = "(none)";
	if (filename == nullptr || filename[0] == '\0')
		filename = "(none)";

	QStringList arguments;

	// plugin type
	arguments.append(QString::fromUtf8(getPluginTypeAsString(type)));

	// filename
	arguments.append(QString::fromUtf8(filename));

	// label
	arguments.append(QString::fromUtf8(label));

	// uniqueId
	arguments.append(QString::number(uniqueId));

	bool started;

	{
		const CarlaScopedEnvVar sev("ENGINE_BRIDGE_SHM_IDS", shmIdsStr);

		blog(LOG_INFO, "[" CARLA_MODULE_ID "] Starting plugin bridge, command is:\n%s \"%s\" \"%s\" \"%s\" " P_INT64,
			bridgeBinary, getPluginTypeAsString(type), filename,
			label, uniqueId);

		started = false;
		proc->setProgram(QString::fromUtf8(bridgeBinary));
		proc->setArguments(arguments);
		QMetaObject::invokeMethod(proc, "start");
		started = proc->waitForStarted(5000) && proc->state() == QProcess::Running;
	}

	if (!started) {
		blog(LOG_INFO, "[" CARLA_MODULE_ID "] failed!");
		QMetaObject::invokeMethod(proc, "stop");
		return false;
	}

	blog(LOG_INFO, "[" CARLA_MODULE_ID "] started ok!");

	ready = false;
	timedOut = false;

	while (proc != nullptr && proc->state() == QProcess::Running && !ready) {
		readMessages();
		carla_msleep(5);
	}

	if (! ready) {
		blog(LOG_WARNING, "[" CARLA_MODULE_ID "] failed to start plugin bridge");
		QMetaObject::invokeMethod(proc, "stop");
		return false;
	}

	childprocess = proc;

	/*if (activated)*/ {
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
	return childprocess != nullptr && childprocess->state() == QProcess::Running;
}

bool carla_bridge::isReady() const noexcept
{
	return ready;
}

bool carla_bridge::idle()
{
	if (childprocess == nullptr)
		return false;

	switch (childprocess->state()) {
	case QProcess::Running: {
		const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

		nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientPing);
		nonRtClientCtrl.commitWrite();
		break;
	}
	case QProcess::NotRunning:
		//         fTimedError = true;
		blog(LOG_INFO, "[" CARLA_MODULE_ID "] bridge closed by itself!");
		// activated = false;
		timedOut = true;
		cleanup();
		return false;
	default:
		return false;
	}

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
	blog(LOG_WARNING, "[" CARLA_MODULE_ID "] waitForClient(%s) timed out", action);
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

void carla_bridge::process(float *buffers[MAX_AV_PLANES], uint32_t frames)
{
	if (!ready || !isRunning())
		return;

	CARLA_SAFE_ASSERT_RETURN(activated, );
	// CARLA_SAFE_ASSERT_RETURN(ready, );

	rtClientCtrl.data->timeInfo.usecs = os_gettime_ns() / 1000;

	for (uint32_t c = 0; c < MAX_AV_PLANES; ++c)
		carla_copyFloats(audiopool.data + (c * bufferSize), buffers[c],
				 frames);

	{
		rtClientCtrl.writeOpcode(kPluginBridgeRtClientProcess);
		rtClientCtrl.writeUInt(frames);
		rtClientCtrl.commitWrite();
	}

	if (wait("process", 1000)) {
		for (uint32_t c = 0; c < MAX_AV_PLANES; ++c)
			carla_copyFloats(buffers[c],
					 audiopool.data + ((c + info.numAudioIns) *
							   bufferSize),
					 frames);
	}
}

void carla_bridge::add_custom_data(const char* const type, const char* const key, const char* const value, const bool sendToPlugin)
{
	CARLA_SAFE_ASSERT_RETURN(type != nullptr && type[0] != '\0',);
	CARLA_SAFE_ASSERT_RETURN(key != nullptr && key[0] != '\0',);
	CARLA_SAFE_ASSERT_RETURN(value != nullptr,);

	// Check if we already have this key
	for (CustomData& cdata : customData)
	{
		if (std::strcmp(cdata.key, key) == 0)
		{
			if (cdata.value != nullptr)
				delete[] cdata.value;

			cdata.value = carla_strdup(value);
			return;
		}
	}

	// Otherwise store it
	CustomData cdata = {};
	cdata.type  = carla_strdup(type);
	cdata.key   = carla_strdup(key);
	cdata.value = carla_strdup(value);
	customData.push_back(cdata);

	if (sendToPlugin)
	{
		const uint32_t typeLen  = static_cast<uint32_t>(std::strlen(type));
		const uint32_t keyLen   = static_cast<uint32_t>(std::strlen(key));
		const uint32_t valueLen = static_cast<uint32_t>(std::strlen(value));

		const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

		nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientSetCustomData);

		nonRtClientCtrl.writeUInt(typeLen);
		nonRtClientCtrl.writeCustomData(type, typeLen);

		nonRtClientCtrl.writeUInt(keyLen);
		nonRtClientCtrl.writeCustomData(key, keyLen);

		nonRtClientCtrl.writeUInt(valueLen);

		if (valueLen > 0)
		{
			if (valueLen > 16384)
			{
				QString filePath(QDir::tempPath());

				filePath += CARLA_OS_SEP_STR ".CarlaCustomData_";
				filePath += audiopool.getFilenameSuffix();

				QFile file(filePath);
				if (file.open(QIODevice::WriteOnly) && file.write(value) != 0)
				{
					const uint32_t ulength = static_cast<uint32_t>(filePath.length());

					nonRtClientCtrl.writeUInt(ulength);
					nonRtClientCtrl.writeCustomData(filePath.toUtf8().constData(), ulength);
				}
				else
				{
					nonRtClientCtrl.writeUInt(0);
				}
			}
			else
			{
				nonRtClientCtrl.writeCustomData(value, valueLen);
			}
		}

		nonRtClientCtrl.commitWrite();

		nonRtClientCtrl.waitIfDataIsReachingLimit();
	}
}

void carla_bridge::custom_data_loaded()
{
	const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

	nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientRestoreLV2State);
	nonRtClientCtrl.commitWrite();
}

void carla_bridge::clear_custom_data()
{
	for (CustomData& cdata : customData)
	{
		delete[] cdata.type;
		delete[] cdata.key;
		delete[] cdata.value;
	}
	customData.clear();
}

void carla_bridge::load_chunk(const char *b64chunk)
{
	chunk = QByteArray::fromBase64(b64chunk);

	QString filePath(QDir::tempPath());

	filePath += CARLA_OS_SEP_STR ".CarlaChunk_";
	filePath += audiopool.getFilenameSuffix();

	QFile file(filePath);
	if (file.open(QIODevice::WriteOnly) && file.write(b64chunk) != 0)
	{
		file.close();

		const uint32_t ulength = static_cast<uint32_t>(filePath.length());

		const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

		nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientSetChunkDataFile);
		nonRtClientCtrl.writeUInt(ulength);
		nonRtClientCtrl.writeCustomData(filePath.toUtf8().constData(), ulength);
		nonRtClientCtrl.commitWrite();

		nonRtClientCtrl.waitIfDataIsReachingLimit();
	}
}

void carla_bridge::save_and_wait()
{
	saved = false;

	{
		const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

		// deactivate bridge client-side ping check
		// some plugins block during save, preventing regular ping timings
		nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientPingOnOff);
		nonRtClientCtrl.writeBool(false);
		nonRtClientCtrl.commitWrite();

		// tell plugin bridge to save and report any pending data
		nonRtClientCtrl.writeOpcode(kPluginBridgeNonRtClientPrepareForSave);
		nonRtClientCtrl.commitWrite();
	}

	while (childprocess != nullptr && childprocess->state() == QProcess::Running && !saved)
	{
		readMessages();
		carla_msleep(5);
		// QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
	}

	if (isRunning())
	{
		const CarlaMutexLocker _cml(nonRtClientCtrl.mutex);

		// reactivate ping check
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
			blog(LOG_DEBUG,
				"carla_bridge::readMessages() - got opcode: %s",
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

			#if 0 // def HAVE_X11
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

		// uint/index, int/rindex, uint/type, uint/hints, short/cc
		case kPluginBridgeNonRtServerParameterData1: {
			const uint32_t index =
				nonRtServerCtrl.readUInt();
			nonRtServerCtrl.readInt();
			const uint32_t type =
				nonRtServerCtrl.readUInt();
			const uint32_t hints =
				nonRtServerCtrl.readUInt();
			nonRtServerCtrl.readShort();

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

				QString realBigValueFilePath(QString::fromUtf8(bigValueFilePath.text));

				#if 0 // ndef CARLA_OS_WIN
				// Using Wine, fix temp dir
				if (fBinaryType == BINARY_WIN32 || fBinaryType == BINARY_WIN64)
				{
					const StringArray driveLetterSplit(StringArray::fromTokens(realBigValueFilePath, ":/", ""));
					blog(LOG_DEBUG, "[" CARLA_MODULE_ID "] big value save path BEFORE => %s", realBigValueFilePath.toRawUTF8());

					realBigValueFilePath  = fWinePrefix;
					realBigValueFilePath += "/drive_";
					realBigValueFilePath += driveLetterSplit[0].toLowerCase();
					realBigValueFilePath += driveLetterSplit[1];

					realBigValueFilePath  = realBigValueFilePath.replace("\\", "/");
					blog(LOG_DEBUG, "[" CARLA_MODULE_ID "] big value save path AFTER => %s", realBigValueFilePath.toRawUTF8());
				}
				#endif

				QFile bigValueFile(realBigValueFilePath);
				CARLA_SAFE_ASSERT_BREAK(bigValueFile.exists());

				if (bigValueFile.open(QIODevice::ReadOnly))
				{
					add_custom_data(type.text, key.text, bigValueFile.readAll().constData(), false);
					bigValueFile.remove();
				}
			}
			else
			{
				const BridgeTextReader value(nonRtServerCtrl, valueSize);

				add_custom_data(type.text, key.text, value.text, false);
			}

		} break;

		// uint/size, str[] (filename, base64 content)
		case kPluginBridgeNonRtServerSetChunkDataFile: {
			// chunkFilePath
			const BridgeTextReader chunkFilePath(nonRtServerCtrl);

			QString realChunkFilePath(QString::fromUtf8(chunkFilePath.text));

			#if 0 // ndef CARLA_OS_WIN
			// Using Wine, fix temp dir
			if (fBinaryType == BINARY_WIN32 || fBinaryType == BINARY_WIN64)
			{
				const StringArray driveLetterSplit(StringArray::fromTokens(realChunkFilePath, ":/", ""));
				blog(LOG_DEBUG, "[" CARLA_MODULE_ID "] chunk save path BEFORE => %s", realChunkFilePath.toRawUTF8());

				realChunkFilePath  = fWinePrefix;
				realChunkFilePath += "/drive_";
				realChunkFilePath += driveLetterSplit[0].toLowerCase();
				realChunkFilePath += driveLetterSplit[1];

				realChunkFilePath  = realChunkFilePath.replace("\\", "/");
				blog(LOG_DEBUG, "[" CARLA_MODULE_ID "] chunk save path AFTER => %s", realChunkFilePath.toRawUTF8());
			}
			#endif

			QFile chunkFile(realChunkFilePath);
			CARLA_SAFE_ASSERT_BREAK(chunkFile.exists());

			if (chunkFile.open(QIODevice::ReadOnly))
			{
				chunk = QByteArray::fromBase64(chunkFile.readAll());
				chunkFile.remove();
			}
		} break;

		// uint/latency
		case kPluginBridgeNonRtServerSetLatency:
			nonRtServerCtrl.readUInt();
			break;

		// uint/index, uint/size, str[] (name)
		case kPluginBridgeNonRtServerSetParameterText: {
			nonRtServerCtrl.readInt();

			if (const uint32_t size = nonRtServerCtrl.readUInt())
				nonRtServerCtrl.skipRead(size);
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
			break;

		// uint/size, str[]
		case kPluginBridgeNonRtServerError: {
			const BridgeTextReader error(nonRtServerCtrl);

			/*
			QMessageBox::critical(nullptr,
					      QString::fromUtf8("Plugin error"),
					      QString::fromUtf8(error.text));
			*/
		} break;
		}
	}
}
