/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../carla-bridge.hpp"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>

#ifndef CARLA_OS_WIN
static QString findWinePrefix(const QString filename,
			      const int recursionLimit = 10)
{
	if (recursionLimit == 0 || filename.length() < 5)
		return {};

	const int lastSep = filename.lastIndexOf('/');
	if (lastSep < 0)
		return {};

	const QString path(filename.left(lastSep));

	if (QFileInfo(path + "/dosdevices").isDir())
		return path;

	return findWinePrefix(path, recursionLimit - 1);
}
#endif

bool carla_bridge::start(const BinaryType btype, const PluginType ptype,
			 const char *label, const char *filename,
			 const int64_t uniqueId)
{
	CARLA_SAFE_ASSERT_RETURN(btype != BINARY_NONE, false);
	CARLA_SAFE_ASSERT_RETURN(ptype != PLUGIN_NONE, false);

	CarlaString bridgeBinary(get_carla_bin_path());

	// ...

	QStringList arguments;

	// ...

#ifndef CARLA_OS_WIN
	// start with "wine" if needed
	if (bridgeBinary.endsWith(".exe")) {
		arguments.append(QString::fromUtf8(bridgeBinary.buffer()));
		bridgeBinary = "wine";

		winePrefix = findWinePrefix(filename);

		if (winePrefix.isEmpty()) {
			const char *const envWinePrefix =
				std::getenv("WINEPREFIX");

			if (envWinePrefix != nullptr &&
			    envWinePrefix[0] != '\0')
				winePrefix = envWinePrefix;
			else
				winePrefix = QDir::homePath() + "/.wine";
		}
	}
#endif

	// ...

	return false;
}

void carla_bridge::readMessages()
{
	while (nonRtServerCtrl.isDataAvailableForReading()) {
		const PluginBridgeNonRtServerOpcode opcode =
			nonRtServerCtrl.readOpcode();

		// ...

		switch (opcode) {

			// ...

		case kPluginBridgeNonRtServerSetCustomData:
			// ...

#ifndef CARLA_OS_WIN
			// Using Wine, fix temp dir
			if (info.btype == BINARY_WIN32 ||
			    info.btype == BINARY_WIN64) {
				const QStringList driveLetterSplit(
					realBigValueFilePath.split(':'));
				blog(LOG_DEBUG,
				     "[" CARLA_MODULE_ID "]"
				     " big value save path BEFORE => %s",
				     realBigValueFilePath.toUtf8().constData());

				realBigValueFilePath = winePrefix;
				realBigValueFilePath += "/drive_";
				realBigValueFilePath +=
					driveLetterSplit[0].toLower();
				realBigValueFilePath += driveLetterSplit[1];

				realBigValueFilePath =
					realBigValueFilePath.replace('\\', '/');
				blog(LOG_DEBUG,
				     "[" CARLA_MODULE_ID "]"
				     " big value save path AFTER => %s",
				     realBigValueFilePath.toUtf8().constData());
			}
#endif

			// ...

		case kPluginBridgeNonRtServerSetChunkDataFile: {
			// ...

#ifndef CARLA_OS_WIN
			// Using Wine, fix temp dir
			if (info.btype == BINARY_WIN32 ||
			    info.btype == BINARY_WIN64) {
				const QStringList driveLetterSplit(
					realChunkFilePath.split(':'));
				blog(LOG_DEBUG,
				     "[" CARLA_MODULE_ID "]"
				     " chunk save path BEFORE => %s",
				     realChunkFilePath.toUtf8().constData());

				realChunkFilePath = winePrefix;
				realChunkFilePath += "/drive_";
				realChunkFilePath +=
					driveLetterSplit[0].toLower();
				realChunkFilePath += driveLetterSplit[1];

				realChunkFilePath =
					realChunkFilePath.replace('\\', '/');
				blog(LOG_DEBUG,
				     "[" CARLA_MODULE_ID "]"
				     " chunk save path AFTER => %s",
				     realChunkFilePath.toUtf8().constData());
			}
#endif

			// ...
		}
		}
	}
}

void carla_bridge::cleanup()
{
	// ...
	winePrefix.clear();
}
