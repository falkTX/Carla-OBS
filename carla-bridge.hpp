/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "CarlaBackend.h"
#include "CarlaBridgeUtils.hpp"

#include <QtCore/QByteArray>
#include <QtCore/QProcess>
#include <QtCore/QString>

#include <vector>

// generates warning if defined as anything else
#define MAX_AV_PLANES 8

CARLA_BACKEND_USE_NAMESPACE

// ----------------------------------------------------------------------------

class BridgeProcess : public QProcess {
    Q_OBJECT

public:
	BridgeProcess();

public Q_SLOTS:
	void start();
	void stop();
};

// ----------------------------------------------------------------------------

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
	BinaryType btype = BINARY_NONE;
	PluginType ptype = PLUGIN_NONE;
	uint32_t hints = 0;
	uint32_t options = PLUGIN_OPTIONS_NULL;
	uint32_t numAudioIns = 0;
	uint32_t numAudioOuts = 0;
	CarlaString filename;
	CarlaString label;
	int64_t uniqueId = 0;

	void clear()
	{
		btype = BINARY_NONE;
		ptype = PLUGIN_NONE;
		hints = 0;
		options = PLUGIN_OPTIONS_NULL;
		numAudioIns = numAudioOuts = 0;
		uniqueId = 0;
		label.clear();
		filename.clear();
	}
};

// ----------------------------------------------------------------------------

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
	QByteArray chunk;
	std::vector<CustomData> customData;

	~carla_bridge()
	{
		delete[] paramDetails;
		clear_custom_data();
	}

	bool init(uint32_t maxBufferSize, double sampleRate);
	void cleanup();

	bool start(BinaryType btype, PluginType ptype,
		   const char *label, const char *filename, int64_t uniqueId);
	bool isRunning() const;
	bool isReady() const noexcept;

	bool idle();

	// waits on RT client, making sure it is still active
	bool wait(const char *action, uint msecs);

	void set_value(uint index, float value);
	void show_ui();

	void activate();
	void deactivate();
	void process(float *buffers[MAX_AV_PLANES], uint32_t frames);

	void add_custom_data(const char *type, const char *key, const char *value, bool sendToPlugin);
	void custom_data_loaded();
	void clear_custom_data();

	void load_chunk(const char *b64chunk);
	void save_and_wait();

private:
	char shmIdsStr[6 * 4 + 1] = {};
	bool activated = false;
	bool ready = false;
	bool saved = false;
	bool timedOut = false;
	uint32_t bufferSize = 0;
	QString winePrefix;

	BridgeAudioPool audiopool;                // fShmAudioPool
	BridgeRtClientControl rtClientCtrl;       // fShmRtClientControl
	BridgeNonRtClientControl nonRtClientCtrl; // fShmNonRtClientControl
	BridgeNonRtServerControl nonRtServerCtrl; // fShmNonRtServerControl

	BridgeProcess *childprocess = nullptr;

	void readMessages();
};

// ----------------------------------------------------------------------------
