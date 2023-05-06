/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "CarlaBackend.h"
#include "CarlaBridgeUtils.hpp"

// #include "water/threads/ChildProcess.h"

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
	PluginType ptype = PLUGIN_NONE;
	uint32_t hints = 0;
	uint32_t options = PLUGIN_OPTIONS_NULL;
	uint32_t numAudioIns = 0;
	uint32_t numAudioOuts = 0;
	CarlaString filename;
	CarlaString label;
	int64_t uniqueId = 0;
	std::vector<uint8_t> chunk;

	void clear()
	{
		ptype = PLUGIN_NONE;
		hints = 0;
		options = PLUGIN_OPTIONS_NULL;
		numAudioIns = numAudioOuts = 0;
		uniqueId = 0;
		label.clear();
		filename.clear();
		chunk.clear();
	}
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

	// cached plugin info
	carla_bridge_info info;

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

	void save_and_wait();
	void load_chunk();

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

	// CarlaScopedPointer<water::ChildProcess> childprocess;

	void readMessages();
};

// --------------------------------------------------------------------------------------------------------------------
