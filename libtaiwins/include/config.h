/*
 * config.h - taiwins config shared header
 *
 * Copyright (c) 2020 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef TW_CONFIG_H
#define TW_CONFIG_H

#include <wayland-server.h>

#include "bindings.h"
#include "engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TW_CONFIG_SHELL_PATH "shell_path"
#define TW_CONFIG_CONSOLE_PATH "console_path"

struct tw_config;
struct tw_config_table;

enum
tw_config_type {
	TW_CONFIG_TYPE_LUA,
	TW_CONFIG_TYPE_DBUS,
};

struct tw_config*
tw_config_create(struct tw_engine *engine, struct tw_bindings *bindings,
                 enum tw_config_type type);

bool
tw_run_config(struct tw_config *config);

bool
tw_run_default_config(struct tw_config *c);

void
tw_config_destroy(struct tw_config *config);

void
tw_config_register_object(struct tw_config *config,
                          const char *name, void *obj);
void *
tw_config_request_object(struct tw_config *config,
                         const char *name);

#ifdef __cplusplus
}
#endif

#endif /* EOF */
