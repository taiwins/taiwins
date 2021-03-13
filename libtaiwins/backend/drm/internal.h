/*
 * internal.h - taiwins server drm backend internal header
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

#ifndef TW_DRM_INTERNAL_H
#define TW_DRM_INTERNAL_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <wayland-server.h>
#include <taiwins/backend_drm.h>
#include <taiwins/backend_libinput.h>
#include <taiwins/render_output.h>
#include <taiwins/objects/plane.h>
#include <taiwins/objects/drm_formats.h>

#include "login/login.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_drm_fb;
struct tw_drm_gpu;
struct tw_drm_plane;
struct tw_drm_display;
struct tw_drm_backend;

#define _DRM_PLATFORM_GBM "TW_DRM_PLATFORM_GBM"
#define _DRM_PLATFORM_STREAM "TW_DRM_PLATFORM_STREAM"
#define TW_DRM_CRTC_ID_INVALID 0
#define TW_DRM_CONN_ID_INVLAID 0
#define TW_DRM_PLANE_ID_INVALID 0
#define TW_DRM_MAX_SWAP_IMGS 3

enum tw_drm_platform {
	TW_DRM_PLATFORM_GBM,
	TW_DRM_PLATFORM_STREAM,
};

enum tw_drm_device_action {
	TW_DRM_DEV_ADD,
	TW_DRM_DEV_RM,
	TW_DRM_DEV_CHANGE,
	TW_DRM_DEV_ONLINE,
	TW_DRM_DEV_OFFLINE,
	TW_DRM_DEV_UNKNOWN,
};

enum tw_drm_features {
	TW_DRM_CAP_ATOMIC = 1 << 0,
	TW_DRM_CAP_PRIME = 1 << 1,
	TW_DRM_CAP_MODIFIERS = 1 << 2,
	TW_DRM_CAP_DUMPBUFFER = 1 << 3,
};

enum tw_drm_plane_type {
	TW_DRM_PLANE_MAJOR,
	TW_DRM_PLANE_OVERLAY,
	TW_DRM_PLANE_CURSOR,
};

enum tw_drm_display_pending_flags {
	TW_DRM_PENDING_ACTIVE = 1 << 1, /**< connector DPMS status changed */
	TW_DRM_PENDING_MODE = 1 << 2,
	TW_DRM_PENDING_CRTC = 1 << 3,
};

enum tw_drm_fb_type {
	TW_DRM_FB_SURFACE,
	TW_DRM_FB_WL_BUFFER,
};

struct tw_drm_prop_info {
	const char *name;
	uint32_t *ptr;
};

struct tw_drm_crtc_props {
	int id;
	//write
	uint32_t active;
	uint32_t mode_id;
};

struct tw_drm_connector_props {
	int id;
	//read
	uint32_t edid;
	uint32_t dpms;
	//write
	uint32_t crtc_id;
};

struct tw_drm_plane_props {
	int id;
	//read
	uint32_t type;
	uint32_t in_formats;

	//write
	uint32_t src_x;
	uint32_t src_y;
	uint32_t src_w;
	uint32_t src_h;
	uint32_t crtc_x;
	uint32_t crtc_y;
	uint32_t crtc_w;
	uint32_t crtc_h;
	uint32_t crtc_id;
	uint32_t fb_id;
};

struct tw_drm_fb {
	enum tw_drm_fb_type type;
	bool locked;
	int fb, x, y, w, h;
	uintptr_t handle;
};

struct tw_drm_plane {
	struct tw_plane base;
	uint32_t crtc_mask;
	enum tw_drm_plane_type type;

	struct tw_drm_formats formats;
	struct tw_drm_plane_props props;
	// struct tw_drm_fb pending, current;
};

struct tw_drm_crtc {
	int idx;
	/** occupied by display */
	struct tw_drm_display *display;
	struct wl_list link; /** drm->crtc_list */

	struct tw_drm_crtc_props props;
};

struct tw_drm_mode_info {
	drmModeModeInfo info;
	struct tw_output_device_mode mode;
};

/**
 * kms_state represents a atomic state we submit to kernel
 */
struct tw_kms_state {

	const struct tw_drm_crtc_props *props_crtc;
	const struct tw_drm_plane_props *props_main_plane;
	const struct tw_drm_connector_props *props_connector;

	struct {
		bool active;
		drmModeModeInfo mode;
		uint32_t mode_id;
	} crtc;
	struct tw_drm_fb fb;
	uint32_t flags;
	//TODO gamma lut
	//TODO list of planes
};

struct tw_drm_display {
	struct tw_render_output output;
	struct tw_drm_backend *drm;
	struct tw_drm_gpu *gpu;

	uint32_t crtc_mask, plane_mask;
	uintptr_t handle; /* platform specific handle */

	/** output has at least one primary plane */
	struct tw_drm_plane *primary_plane;
	struct tw_drm_crtc *crtc;

	struct {
		bool active;
		//int crtc_id; /**< crtc_id read from connector, may not work */
		/* crtc for unset, used once in display_stop */
		struct tw_drm_crtc *unset_crtc;
		struct wl_array modes;
		struct tw_kms_state kms_current, kms_pending;
		enum tw_drm_display_pending_flags pending;

	} status;
	//TODO remove the swapchain here
	//struct tw_drm_swapchain sc;
	struct tw_drm_connector_props props;

	struct wl_listener presentable_commit;
};

struct tw_drm_gpu_impl {
	enum tw_drm_platform type;
	/** get different device handles (gbm or egl_device) */
	bool (*get_gpu_device)(struct tw_drm_gpu *,
	                       const struct tw_login_gpu *);
	void (*free_gpu_device)(struct tw_drm_gpu *);
	/** platform specific egl options */
	const struct tw_egl_options *(*gen_egl_params)(struct tw_drm_gpu *);
	/** init display buffers as well as render surface */
	bool (*allocate_fb)(struct tw_drm_display *output,
	                    drmModeModeInfo *mode);
	//release buffer
	void (*release_fb)(struct tw_drm_display *output,
	                   struct tw_drm_fb *fb);
	/** destroy display buffers as well as render surface */
	void (*end_display)(struct tw_drm_display *output);

	bool (*compose_fb)(struct tw_drm_display *output,
	                   struct tw_kms_state *state);
};

struct tw_drm_gpu {
	int gpu_fd;
	dev_t devnum;
	uintptr_t device; /**< contains platform device type */
	bool boot_vga;
	bool activated; /**< valid gpu otherwise not used */
	uint32_t visual_id;
	clockid_t clk_id;

	struct wl_list link; /* backend:gpu_list */

	const struct tw_drm_gpu_impl *impl;
	enum tw_drm_features feats;
	struct tw_drm_backend *drm;
	struct wl_event_source *event;

	struct {
		int max_width, max_height;
		int min_width, min_height;
	} limits;

	struct tw_drm_crtc crtcs[32];
	uint32_t crtc_mask;
	struct wl_list crtc_list;

	uint32_t plane_mask;
	struct tw_drm_plane planes[32];
	struct wl_list plane_list;

	struct tw_drm_connector_props props;
};

/**
 * @brief drm backend datasheet
 *
 * We need the backend to support multiple allocation APIs(gbm or eglstream),
 * multiple rendering API (egl or vulkan).
 * commit routine (atomic or legacy)
 *
 * The 3 event sources:
 * 1): drm fd for drmHandleEvents.
 * 2): event from login for session changes.
 * 3): event from udev for udev_device change.
 */
struct tw_drm_backend {
	struct tw_backend base;
	struct wl_display *display;
	struct tw_login *login;
	struct tw_drm_gpu *boot_gpu;
	struct wl_list gpu_list;
	struct tw_libinput_input input;

	struct wl_listener display_destroy;
	struct wl_listener login_attribute_change;
	struct wl_listener udev_device_change;
};

/******************************* resource API ********************************/

void
tw_drm_print_info(int fd);

enum tw_drm_device_action
tw_drm_device_action_from_name(const char *name);

void
tw_drm_backend_remove_gpu(struct tw_drm_gpu *gpu);

int
tw_drm_handle_drm_event(int fd, uint32_t mask, void *data);

void
tw_drm_handle_gpu_event(struct tw_drm_gpu *gpu,
                        enum tw_drm_device_action action);
void
tw_drm_free_gpu_resources(struct tw_drm_gpu *gpu);

bool
tw_drm_check_gpu_features(struct tw_drm_gpu *gpu);

/** the function destroys the output, which is fine, since we either call
 * this function on gpu init fail or destruction. */
bool
tw_drm_check_gpu_resources(struct tw_drm_gpu *gpu);

/**
 * scan the properties by binary search, info has to be ordered
 */
bool
tw_drm_read_properties(int fd, uint32_t obj_id, uint32_t obj_type,
                       struct tw_drm_prop_info *info, size_t prop_len);
bool
tw_drm_get_property(int fd, uint32_t obj_id, uint32_t obj_type,
                    const char *prop_name, uint64_t *value);

/********************************* plane API *********************************/

bool
tw_drm_plane_init(struct tw_drm_plane *plane, int fd, drmModePlane *drm_plane);

void
tw_drm_plane_fini(struct tw_drm_plane *plane);

/******************************** display API ********************************/

void
tw_drm_display_start(struct tw_drm_display *display);

void
tw_drm_display_continue(struct tw_drm_display *display);

void
tw_drm_display_pause(struct tw_drm_display *output);

void
tw_drm_display_stop(struct tw_drm_display *output);

void
tw_drm_display_remove(struct tw_drm_display *display);

struct tw_drm_display *
tw_drm_display_find_create(struct tw_drm_gpu *gpu, drmModeConnector *conn);

bool
tw_drm_display_read_info(struct tw_drm_display *output,
                         drmModeConnector *conn);
void
tw_drm_display_check_action(struct tw_drm_display *output,
                            drmModeConnector *conn,
                            bool *need_start, bool *need_stop,
                            bool *need_continue, bool *need_remove);

/********************************** KMS API **********************************/

void
tw_kms_state_copy(struct tw_kms_state *dst, struct tw_kms_state *src,
                  int drm_fd);
bool
tw_kms_state_submit_atomic(struct tw_kms_state *state,
                           struct tw_drm_display *output, uint32_t flags);
bool
tw_kms_state_submit_legacy(struct tw_kms_state *state,
                           struct tw_drm_display *output, uint32_t flags);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
