/*
 * Carla plugin for OBS
 * Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// needed for libdl stuff
#if !defined(_GNU_SOURCE) && !defined(_WIN32)
#define _GNU_SOURCE
#endif

#include "common.h"

#include <obs-module.h>
#include <util/platform.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#endif

#ifdef _MSC_VER
#define strdup _strdup
#endif

// ----------------------------------------------------------------------------

#ifndef _WIN32
static const struct {
	const char *bin;
	const char *res;
} carla_system_paths[] = {
#ifdef __APPLE__
	{ "~/Applications/Carla.app/Contents/MacOS", "~/Applications/Carla.app/Contents/MacOS/resources" },
	{ "/Applications/Carla.app/Contents/MacOS", "/Applications/Carla.app/Contents/MacOS/resources" },
#endif
	{ "/usr/local/lib/carla", "/usr/local/share/carla/resources" },
	{ "/usr/lib/carla", "/usr/share/carla/resources" },
};
#endif

static char* module_path = NULL;
static const char* resource_path = NULL;

const char *get_carla_bin_path(void)
{
	if (module_path != NULL)
		return module_path;

#ifdef _WIN32
#else
	Dl_info info;
	dladdr(get_carla_bin_path, &info);
	module_path = realpath(info.dli_fname, NULL);
#endif

	if (module_path == NULL)
		goto fail;

	// find last separator
	char *lastsep = strrchr(module_path, '/');
	if (lastsep == NULL)
		goto free;

	// truncate to ".../carla"
	for (int i = 0; i < 6 /* strlen("/carla") */; i++) {
		if (*lastsep == '\0')
			goto free;
		++lastsep;
	}
	*lastsep = '\0';

	if (os_file_exists(module_path))
		return module_path;

free:
	free(module_path);
	module_path = NULL;

fail:
#ifndef _WIN32
	for (size_t i = 0; i < sizeof(carla_system_paths)/sizeof(carla_system_paths[0]); ++i) {
		if (os_file_exists(carla_system_paths[i].bin)) {
			/* NOTE we are intentionally not using bstrdup,
			   as a previous allocation could be done by `realpath`
			   so we keep everything `free` compatible. */
			module_path = strdup(carla_system_paths[i].bin);
			resource_path = carla_system_paths[i].res;
			break;
		}
	}
#endif

	return module_path;
}

const char *get_carla_resource_path(void)
{
	if (resource_path != NULL)
		return resource_path;

#ifndef _WIN32
	for (size_t i = 0; i < sizeof(carla_system_paths)/sizeof(carla_system_paths[0]); ++i) {
		if (os_file_exists(carla_system_paths[i].res)) {
			resource_path = carla_system_paths[i].res;
			break;
		}
	}
#endif

	return resource_path;
}

void param_index_to_name(uint32_t index, char name[PARAM_NAME_SIZE])
{
	name[1] = '0' + ((index / 100) % 10);
	name[2] = '0' + ((index / 10) % 10);
	name[3] = '0' + ((index / 1) % 10);
}

void remove_all_props(obs_properties_t *props, obs_data_t *settings)
{
	obs_data_erase(settings, PROP_SHOW_GUI);
	obs_properties_remove_by_name(props, PROP_SHOW_GUI);

	obs_data_erase(settings, PROP_CHUNK);
	obs_properties_remove_by_name(props, PROP_CHUNK);

	obs_data_erase(settings, PROP_CUSTOM_DATA);
	obs_properties_remove_by_name(props, PROP_CUSTOM_DATA);

	char pname[PARAM_NAME_SIZE] = PARAM_NAME_INIT;

	for (uint32_t i = 0; i < MAX_PARAMS; ++i) {
		param_index_to_name(i, pname);
		obs_data_erase(settings, pname);
		obs_data_unset_default_value(settings, pname);
		obs_properties_remove_by_name(props, pname);
	}
}

void postpone_update_request(uint64_t *update_req)
{
	*update_req = os_gettime_ns();
}

void handle_update_request(obs_source_t *source, uint64_t *update_req)
{
	const uint64_t old_update_req = *update_req;

	if (old_update_req == 0)
		return;

	const uint64_t now = os_gettime_ns();

	// request in the future?
	if (now < old_update_req) {
		*update_req = now;
		return;
	}

	if (now - old_update_req >= 100000000ULL) // 100ms
	{
		*update_req = 0;

		signal_handler_t *sighandler =
			obs_source_get_signal_handler(source);
		signal_handler_signal(sighandler, "update_properties",
					NULL);
	}
}

void obs_module_unload(void)
{
	free(module_path);
	module_path = NULL;
}

// ----------------------------------------------------------------------------
