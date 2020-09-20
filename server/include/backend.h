/*
 * backend.h - taiwins server backend header
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

#ifndef TW_BACKEND_H
#define TW_BACKEND_H

#include <stdint.h>
#include <strings.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/render/wlr_renderer.h>
#include <xkbcommon/xkbcommon.h>

#include <taiwins/objects/matrix.h>
#include <taiwins/objects/surface.h>
#include <taiwins/objects/layers.h>
#include <taiwins/objects/data_device.h>
#include <taiwins/objects/compositor.h>
#include <taiwins/objects/dmabuf.h>
#include <taiwins/objects/cursor.h>
#include <taiwins/objects/presentation_feedback.h>
#include <taiwins/objects/viewporter.h>

#define TW_VIEW_GLOBAL_LINK 1
#define TW_VIEW_OUTPUT_LINK 2

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_backend;
struct tw_backend_output;
struct tw_backend_seat;

enum tw_backend_event_type {
	TW_BACKEND_ADD_OUTPUT,
	TW_BACKEND_RM_OUTPUT,
	TW_BACKEND_ADD_SEAT,
	TW_BACKEND_RM_SEAT,
	TW_BACKEND_CH_SEAT,
};

struct tw_backend_output_mode {
	int32_t w, h, refresh;
};

/* we would have list of seats in  */
enum tw_input_device_cap {
	TW_INPUT_CAP_KEYBOARD = 1 << 0,
	TW_INPUT_CAP_POINTER = 1 << 1,
	TW_INPUT_CAP_TOUCH = 1 << 2,
	TW_INPUT_CAP_TABLET_TOOL = 1 << 3,
	TW_INPUT_CAP_TABLET_PAD = 1 << 4,
	TW_INPUT_CAP_SWITCH = 1 << 5,
	TW_INPUT_CAP_ALL = 0x1f,
};

/**
 * @brief backend output
 *
 * backend has the logic of output, for now, we take advantage of wlr_output,
 * which implements wl_output.
 */
struct tw_backend_output {
	struct tw_backend *backend;
	struct wlr_output *wlr_output;
	int32_t id, cloning;
	struct wl_list link; /* tw_backend:heads */
	struct wl_list views; /** tw_surface->output_link */
	char name[24];

	//we can do this, or we uses current state
	struct {
		bool dirty;
		bool activate;
		bool preferred_mode;
		/* set by user, corrected by backend */
		int32_t x, y, w, h, refresh;
		float scale;
		enum wl_output_transform transform;
		struct tw_cursor_constrain constrain;
		/**< we have 3 frame_damages for triple buffering */
		pixman_region32_t damages[3];
		pixman_region32_t *pending_damage, *curr_damage, *prev_damage;

		//TODO set gamma, wlroots uses gamma for redshift.
		float gamma_value;

		/* convert global coordinates to output GL coordinates */
		struct tw_mat3 view_2d;

		/** the repaint status, the output repaint is driven by timer,
		 * in the future we may be able to drive it by idle event */
		enum {
			TW_REPAINT_CLEAN = 0, /**< no need to repaint */
			TW_REPAINT_DIRTY, /**< repaint required */
			TW_REPAINT_NOT_FINISHED /**< still in repaint */
		} repaint_state;
	} state;

	struct wl_listener frame_listener;
	struct wl_listener destroy_listener;
};

//TODO: remove the listeners in tw_backend_seat, deligate those listeners to
//tw_input_events
struct tw_backend_seat {
	int idx;
	struct wl_list link; /* tw_backend.inputs */
	uint32_t capabilities;
	struct tw_backend *backend;

	struct tw_seat *tw_seat; /**< tw_seat implments wl_seat protocol */
	struct {
		struct wlr_input_device *device;
		struct wl_listener destroy;
		struct wl_listener modifiers;
		struct wl_listener key;
		/** xkb info */
		struct xkb_rule_names rules;

	} keyboard;

	struct {
		struct wlr_input_device *device;
		struct wl_listener destroy;
		struct wl_listener button;
		struct wl_listener motion;
		struct wl_listener motion_abs;
		struct wl_listener axis;
		struct wl_listener frame;
	} pointer;

	struct {
		struct wlr_input_device *device;
		struct wl_listener destroy;
		struct wl_listener down;
		struct wl_listener up;
		struct wl_listener motion;
		struct wl_listener cancel;
	} touch;
};

struct tw_backend_impl;
struct tw_backend {
	struct wl_display *display;
        /** for now backend is based on wlr_backends. Future when migrate, we
         * would used api like wayland_impl_tw_backend(struct tw_backend *);
         */
	struct wlr_backend *auto_backend;
	struct wlr_renderer *main_renderer;
	/** interface for implementation details */
	struct tw_backend_impl *impl;
	bool started;
	/**< options */
	bool defer_output_creation;

	struct wl_listener display_destroy_listener;

	struct wl_signal output_frame_signal;
        struct wl_signal output_plug_signal;
	struct wl_signal output_unplug_signal;
	struct wl_signal seat_add_signal;
	struct wl_signal seat_ch_signal;
	struct wl_signal seat_rm_signal;

	/* outputs */
	struct wl_list heads; /* tw_backend_output:links */
	struct wl_list pending_heads;
	uint32_t output_pool;
	struct tw_backend_output outputs[32];

        /* inputs */
	struct xkb_context *xkb_context;
	struct wl_list inputs; /* tw_backend_seat:links */
	uint8_t seat_pool;
	struct tw_backend_seat seats[8];
        /** cursor is global, like most desktop experience, the one reason is
         * that people want to fit cursor in the cursor plane.
         */
	struct tw_cursor global_cursor;

	/** the basic objects required by a compositor **/
	struct tw_surface_manager surface_manager;
	struct tw_layers_manager layers_manager;
	struct tw_compositor compositor_manager;
	struct tw_data_device_manager data_device_manager;
	struct tw_linux_dmabuf dma_engine;
	struct tw_presentation presentation;
	struct tw_viewporter viewporter;
};

struct tw_backend *
tw_backend_create_global(struct wl_display *display,
                         wlr_renderer_create_func_t render_create);
void
tw_backend_flush(struct tw_backend *backend);

//remove this!
void *
tw_backend_get_backend(struct tw_backend *backend);

struct tw_backend_output *
tw_backend_focused_output(struct tw_backend *backend);

struct tw_backend_output *
tw_backend_output_from_cursor_pos(struct tw_backend *backend);

struct tw_backend_output *
tw_backend_output_from_resource(struct wl_resource *resource);

void
tw_backend_defer_outputs(struct tw_backend *backend, bool defer);

void
tw_backend_commit_output_state(struct tw_backend_output *o);

void
tw_backend_add_listener(struct tw_backend *backend,
                        enum tw_backend_event_type event,
                        struct wl_listener *listener);
struct tw_backend_output *
tw_backend_find_output(struct tw_backend *backend, const char *name);

struct tw_surface *
tw_backend_pick_surface_from_layers(struct tw_backend *backend,
                                    float x, float y,
                                    float *sx,  float *sy);
/**
 * @brief build the surface list, top down fashion based on layers
 */
void
tw_backend_build_surface_list(struct tw_backend *backend);

void
tw_backend_set_output_scale(struct tw_backend_output *output, float scale);

void
tw_backend_set_output_transformation(struct tw_backend_output *output,
                                     enum wl_output_transform transform);
int
tw_backend_get_output_modes(struct tw_backend_output *output,
                            struct tw_backend_output_mode *modes);
void
tw_backend_set_output_mode(struct tw_backend_output *output,
                           const struct tw_backend_output_mode *mode);
void
tw_backend_set_output_position(struct tw_backend_output *output,
                               uint32_t x, uint32_t y);
void
tw_backend_output_clone(struct tw_backend_output *dst,
                        const struct tw_backend_output *src);
void
tw_backend_output_enable(struct tw_backend_output *output,
                         bool enable);
//gamma or color temperature ?
void
tw_backend_output_set_gamma(struct tw_backend_output *output,
                            float gamma);
void
tw_backend_output_dirty(struct tw_backend_output *output);

struct wl_resource *
tw_backend_output_get_wl_output(struct tw_backend_output *output,
                                struct wl_resource *reference);
struct tw_backend_seat *
tw_backend_get_focused_seat(struct tw_backend *backend);

void
tw_backend_seat_set_xkb_rules(struct tw_backend_seat *seat,
                              struct xkb_rule_names *rules);
void
tw_backend_set_repeat_info(struct tw_backend *backend,
                           unsigned int rate, unsigned int delay);
void
tw_backend_switch_session(struct tw_backend *backend, uint32_t session);

#ifdef  __cplusplus
}
#endif


#endif /* EOF */
