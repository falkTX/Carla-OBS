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

// FIXME
struct carla_bridge_info {
	uint32_t aIns, aOuts;
	uint32_t cvIns, cvOuts;
	uint32_t mIns, mOuts;
	PluginCategory category;
	uint optionsAvailable;
	CarlaString name;
	CarlaString label;
	CarlaString maker;
	CarlaString copyright;
	const char **aInNames;
	const char **aOutNames;
	const char **cvInNames;
	const char **cvOutNames;
	std::vector<uint8_t> chunk;

	carla_bridge_info()
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
		  chunk()
	{
	}

	~carla_bridge_info() { clear(); }

	void clear()
	{
		if (aInNames != nullptr) {
			CARLA_SAFE_ASSERT_INT(aIns > 0, aIns);

			for (uint32_t i = 0; i < aIns; ++i)
				delete[] aInNames[i];

			delete[] aInNames;
			aInNames = nullptr;
		}

		if (aOutNames != nullptr) {
			CARLA_SAFE_ASSERT_INT(aOuts > 0, aOuts);

			for (uint32_t i = 0; i < aOuts; ++i)
				delete[] aOutNames[i];

			delete[] aOutNames;
			aOutNames = nullptr;
		}

		if (cvInNames != nullptr) {
			CARLA_SAFE_ASSERT_INT(cvIns > 0, cvIns);

			for (uint32_t i = 0; i < cvIns; ++i)
				delete[] cvInNames[i];

			delete[] cvInNames;
			cvInNames = nullptr;
		}

		if (cvOutNames != nullptr) {
			CARLA_SAFE_ASSERT_INT(cvOuts > 0, cvOuts);

			for (uint32_t i = 0; i < cvOuts; ++i)
				delete[] cvOutNames[i];

			delete[] cvOutNames;
			cvOutNames = nullptr;
		}

		aIns = aOuts = cvIns = cvOuts = 0;
	}

	CARLA_DECLARE_NON_COPYABLE(carla_bridge_info)
};

struct carla_bridge_callback {
	virtual ~carla_bridge_callback(){};
	virtual void bridge_parameter_changed(uint index, float value) = 0;
};

struct carla_bridge {
	carla_bridge_callback *callback = nullptr;

	// cached parameter info
	uint32_t paramCount = 0;
	struct carla_param_data *paramDetails = nullptr;

	// FIXME
	carla_bridge_info fInfo;

	~carla_bridge() { delete[] paramDetails; }

	bool init(uint32_t maxBufferSize, double sampleRate);
	void cleanup();

	bool start(PluginType type, const char *binaryArchName,
		   const char *bridgeBinary, const char *label,
		   const char *filename, int64_t uniqueId);
	bool isRunning() const;
	bool isReady() const noexcept;

	bool idle();

	// waits on RT client, making sure it is still active
	bool wait(const char *action, uint msecs);

	void set_value(uint index, float value);
	void show_ui();

	void activate();
	void deactivate();
	void process(float *buffers[2], uint32_t frames);

private:
	char shmIdsStr[6 * 4 + 1] = {};
	bool activated = false;
	bool ready = false;
	bool saved = false;
	bool timedOut = false;
	uint32_t bufferSize = 0;

	BridgeAudioPool audiopool;                // fShmAudioPool
	BridgeRtClientControl rtClientCtrl;       // fShmRtClientControl
	BridgeNonRtClientControl nonRtClientCtrl; // fShmNonRtClientControl
	BridgeNonRtServerControl nonRtServerCtrl; // fShmNonRtServerControl

	CarlaScopedPointer<water::ChildProcess> childprocess;

	void readMessages();
};

// --------------------------------------------------------------------------------------------------------------------
