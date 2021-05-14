/*
 * seat.c - taiwins engine seat implementation
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

#include <assert.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/logger.h>
#include <taiwins/objects/seat.h>

#include <taiwins/engine.h>
#include <taiwins/backend.h>
#include <taiwins/input_device.h>
#include <taiwins/output_device.h>

#include "utils.h"
#include "internal.h"

static inline bool
seat_has_keyboard(struct tw_seat *seat)
{
	return (seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD);
}

static inline bool
seat_has_pointer(struct tw_seat *seat)
{
	return (seat->capabilities & WL_SEAT_CAPABILITY_POINTER);
}

static inline bool
seat_has_touch(struct tw_seat *seat)
{
	return (seat->capabilities & WL_SEAT_CAPABILITY_TOUCH);
}

static void
idle_refocus(void *data)
{
	struct tw_engine_seat *seat = data;
	struct tw_seat *tw_seat = seat->tw_seat;
	struct tw_cursor *cursor = &seat->engine->global_cursor;
	float x = cursor->x;
	float y = cursor->y;
	struct tw_surface *focused =
		tw_engine_pick_surface_from_layers(seat->engine, x, y, &x, &y);

        if (!focused)
		return;
	if (!tw_seat->keyboard.focused_surface && seat_has_keyboard(tw_seat))
		tw_keyboard_set_focus(&tw_seat->keyboard, focused->resource,
		                      NULL);
	if (!tw_seat->pointer.focused_surface && seat_has_pointer(tw_seat))
		tw_pointer_set_focus(&tw_seat->pointer, focused->resource,
		                     x, y);
	if (!tw_seat->touch.focused_surface && seat_has_touch(tw_seat))
		tw_touch_set_focus(&tw_seat->touch, focused->resource, x, y);
}

static void
notify_seat_remove_device(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.remove);
	struct tw_backend *backend = seat->engine->backend;
	struct tw_input_device *current = data, *device;
	bool device_type_left = false;

	wl_list_for_each(device, &backend->inputs, link) {
		if ((int)device->seat_id == seat->idx && device != current) {
			if (device->type == current->type) {
				device_type_left = true;
				break;
			}
		}
	}
	//this is a bit unplesant
	if (!device_type_left) {
		if (current->type == TW_INPUT_TYPE_KEYBOARD)
			tw_seat_remove_keyboard(seat->tw_seat);
		else if (current->type == TW_INPUT_TYPE_POINTER)
			tw_seat_remove_pointer(seat->tw_seat);
		else if (current->type == TW_INPUT_TYPE_TOUCH)
			tw_seat_remove_touch(seat->tw_seat);
	}
}

static void
notify_seat_input_event(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.event);
	wl_signal_emit(&seat->engine->signals.seat_input, seat);
}

static void
notify_seat_focus_device(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, listeners.focus);
	struct tw_engine *engine = seat->engine;

	if (engine->focused_seat != seat) {
		engine->focused_seat = seat;
		wl_signal_emit(&engine->signals.seat_focused, seat);
	}
}

static void
notify_seat_unfocus_device(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, listeners.unfocus);
	struct wl_event_loop *loop =
		wl_display_get_event_loop(seat->engine->display);

	wl_event_loop_add_idle(loop, idle_refocus, seat);
}

/******************************************************************************
 * keyboard listeners
 *****************************************************************************/
static uint32_t
get_modmask(struct tw_input_device *device)
{
	uint32_t mask = 0;
	struct xkb_state *state = device->input.keyboard.keystate;

	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT,
	                                 XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_MODIFIER_ALT;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL,
	                                 XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_MODIFIER_CTRL;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO,
	                                 XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_MODIFIER_SUPER;
	if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT,
	                                 XKB_STATE_MODS_EFFECTIVE))
		mask |= TW_MODIFIER_SHIFT;
	return mask;
}

static uint32_t
get_ledmask(struct tw_input_device *device)
{
	uint32_t mask = 0;
	struct xkb_state *state = device->input.keyboard.keystate;

	if (xkb_state_led_name_is_active(state, XKB_LED_NAME_NUM))
		mask |= TW_LED_NUM_LOCK;
	if (xkb_state_led_name_is_active(state, XKB_LED_NAME_CAPS))
		mask |= TW_LED_CAPS_LOCK;
	if (xkb_state_led_name_is_active(state, XKB_LED_NAME_SCROLL))
		mask |= TW_LED_SCROLL_LOCK;
	return mask;
}

static void
notify_seat_keyboard_modifiers(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.keyboard.modifiers);
	struct tw_keyboard *seat_keyboard = &seat->tw_seat->keyboard;
	struct tw_event_keyboard_modifier *event = data;

	seat_keyboard->modifiers_state = get_modmask(event->dev);
	seat_keyboard->led_state = get_ledmask(event->dev);

	tw_keyboard_notify_modifiers(seat_keyboard,
	                             event->depressed, event->latched,
	                             event->locked, event->group);
}

static void
notify_seat_keyboard_key(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.keyboard.key);
	struct tw_keyboard *seat_keyboard = &seat->tw_seat->keyboard;
	struct tw_event_keyboard_key *event = data;

	tw_keyboard_notify_key(seat_keyboard, event->time, event->keycode,
	                       event->state);
}

/******************************************************************************
 * pointer listeners
 *****************************************************************************/

static void
notify_seat_pointer_button(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.pointer.button);
	struct tw_event_pointer_button *event = data;
	struct tw_pointer *seat_pointer = &seat->tw_seat->pointer;

	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED)
		seat_pointer->btn_count++;
	else
		seat_pointer->btn_count--;
	tw_pointer_notify_button(seat_pointer, event->time, event->button,
	                         event->state);
}

static void
pointer_focus_motion(struct tw_engine_seat *seat, uint32_t timespec)
{
	struct tw_pointer *pointer = &seat->tw_seat->pointer;
	struct tw_surface *focused = pointer->focused_surface ?
		tw_surface_from_resource(pointer->focused_surface) : NULL;
	float x = seat->engine->global_cursor.x;
	float y = seat->engine->global_cursor.y;

	if (pointer->grab && pointer->grab->impl->enter)
		focused = tw_engine_pick_surface_from_layers(seat->engine,
		                                             x, y, &x, &y);
	else if (focused)
		tw_surface_to_local_pos(focused, x, y, &x, &y);

	if (focused && (pointer->focused_surface == focused->resource))
			tw_pointer_notify_motion(pointer, timespec, x, y);
	else if (focused)
		tw_pointer_notify_enter(pointer, focused->resource, x, y);
}

static void
notify_seat_pointer_motion(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.pointer.motion);
	struct tw_engine *engine = seat->engine;
	struct tw_event_pointer_motion *event = data;

	SCOPE_PROFILE_BEG();

	//TODO: this is probably not right, relative motion only works for
	//libinput
	tw_cursor_move(&engine->global_cursor,
	               event->delta_x, event->delta_y);
	pointer_focus_motion(seat, event->time);

	SCOPE_PROFILE_END();
}

static void
notify_seat_pointer_motion_abs(struct wl_listener *listener, void *data)
{
	float x, y;
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.pointer.motion_absolute);
	struct tw_event_pointer_motion_abs *event = data;
	struct tw_engine_output *output =
		tw_engine_output_from_device(seat->engine, event->output);
	tw_output_device_loc_to_global(output->device, event->x, event->y,
	                               &x, &y);
	SCOPE_PROFILE_BEG();

	tw_cursor_set_pos(&seat->engine->global_cursor, x, y);
	pointer_focus_motion(seat, event->time_msec);

	SCOPE_PROFILE_END();
}

static void
notify_seat_pointer_axis(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.pointer.axis);
	struct tw_pointer *pointer = &seat->tw_seat->pointer;
	struct tw_event_pointer_axis *event = data;

	tw_pointer_notify_axis(pointer, event->time,
	                       event->axis, event->delta,
	                       (int)event->delta_discrete, event->source);

}

static void
notify_seat_pointer_frame(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.pointer.frame);
	struct tw_pointer *seat_pointer = &seat->tw_seat->pointer;
	tw_pointer_notify_frame(seat_pointer);
}

static void
notify_seat_pointer_pinch_begin(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.pointer.pinch_begin);
	struct tw_event_pointer_gesture *event = data;
	struct tw_pointer *pointer = &seat->tw_seat->pointer;
	struct tw_gestures_manager *gs_manager =
		&seat->engine->gestures_manager;
	struct wl_resource *wl_surface =
		seat->tw_seat->pointer.focused_surface;

	tw_gestures_pinch_begin(gs_manager, pointer, event->time, wl_surface,
	                        event->fingers);
}

static void
notify_seat_pointer_pinch_update(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.pointer.pinch_update);
	struct tw_event_pointer_gesture *event = data;
	struct tw_pointer *pointer = &seat->tw_seat->pointer;
	struct tw_gestures_manager *gs_manager =
		&seat->engine->gestures_manager;

	tw_gestures_pinch_update(gs_manager, pointer, event->time,
	                         event->dx, event->dy, event->scale,
	                         event->rotation);
}

static void
notify_seat_pointer_pinch_end(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.pointer.pinch_end);
	struct tw_event_pointer_gesture *event = data;
	struct tw_pointer *pointer = &seat->tw_seat->pointer;
	struct tw_gestures_manager *gs_manager =
		&seat->engine->gestures_manager;

	tw_gestures_pinch_end(gs_manager, pointer, event->time,
	                      event->cancelled);
}

static void
notify_seat_pointer_swipe_begin(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.pointer.swipe_begin);
	struct tw_event_pointer_gesture *event = data;
	struct tw_pointer *pointer = &seat->tw_seat->pointer;
	struct tw_gestures_manager *gs_manager =
		&seat->engine->gestures_manager;
	struct wl_resource *wl_surface =
		seat->tw_seat->pointer.focused_surface;

	tw_gestures_swipe_begin(gs_manager, pointer, event->time, wl_surface,
	                        event->fingers);
}

static void
notify_seat_pointer_swipe_update(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.pointer.swipe_update);
	struct tw_event_pointer_gesture *event = data;
	struct tw_pointer *pointer = &seat->tw_seat->pointer;
	struct tw_gestures_manager *gs_manager =
		&seat->engine->gestures_manager;

	tw_gestures_swipe_update(gs_manager, pointer, event->time,
	                         event->dx, event->dy);
}

static void
notify_seat_pointer_swipe_end(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.pointer.swipe_end);
	struct tw_event_pointer_gesture *event = data;
	struct tw_pointer *pointer = &seat->tw_seat->pointer;
	struct tw_gestures_manager *gs_manager =
		&seat->engine->gestures_manager;

	tw_gestures_swipe_end(gs_manager, pointer, event->time,
	                      event->cancelled);
}

/******************************************************************************
 * touch listeners
 *****************************************************************************/

static void
notify_seat_touch_down(struct wl_listener *listener, void *data)
{
	float x, y;
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.touch.down);
	struct tw_surface *focused;
	struct tw_touch *touch = &seat->tw_seat->touch;
	struct tw_event_touch_down *event = data;
	struct tw_engine_output *output =
		tw_engine_output_from_device(seat->engine, event->output);

        tw_output_device_loc_to_global(output->device, event->x, event->y,
	                               &x, &y);
	tw_cursor_set_pos(&seat->engine->global_cursor, x, y);

	focused = tw_engine_pick_surface_from_layers(seat->engine,
	                                              x, y, &x, &y);
	if (focused && focused->resource == touch->focused_surface)
		tw_touch_notify_down(touch, event->time,
			                        event->touch_id, x, y);
	else if (focused) {
		tw_touch_notify_enter(touch, focused->resource, x, y);
		tw_touch_notify_down(touch, event->time,
		                     event->touch_id, x, y);
	}
}

static void
notify_seat_touch_up(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.touch.up);
	struct tw_event_touch_up *event = data;
	struct tw_touch *touch = &seat->tw_seat->touch;

	tw_touch_notify_up(touch, event->time, event->touch_id);
}

static void
notify_seat_touch_motion(struct wl_listener *listener, void *data)
{
	float x, y;
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.touch.motion);
	struct tw_event_touch_motion *event = data;
	struct tw_touch *touch = &seat->tw_seat->touch;
	struct tw_surface *focused;
	struct tw_engine_output *output =
		tw_engine_output_from_device(seat->engine, event->output);

	tw_output_device_loc_to_global(output->device, event->x, event->y,
	                               &x, &y);
	if (touch->focused_surface) {
		focused = tw_surface_from_resource(touch->focused_surface);
		tw_surface_to_local_pos(focused, x, y, &x, &y);
		tw_touch_notify_motion(touch, event->time,
		                       event->touch_id, x, y);
	}
}

static void
notify_seat_touch_cancel(struct wl_listener *listener, void *data)
{
	struct tw_engine_seat *seat =
		wl_container_of(listener, seat, sink.touch.cancel);
	struct tw_touch *touch = &seat->tw_seat->touch;
	tw_touch_notify_cancel(touch);
}


/******************************************************************************
 * internal APIs
 *****************************************************************************/

static inline void
seat_install_keyboard_listeners(struct tw_engine_seat *seat)
{
	tw_signal_setup_listener(&seat->source.keyboard.key,
	                         &seat->sink.keyboard.key,
	                         notify_seat_keyboard_key);
	tw_signal_setup_listener(&seat->source.keyboard.modifiers,
	                         &seat->sink.keyboard.modifiers,
	                         notify_seat_keyboard_modifiers);
}

static void
seat_install_pointer_listeners(struct tw_engine_seat *seat)
{
	tw_signal_setup_listener(&seat->source.pointer.button,
	                         &seat->sink.pointer.button,
	                         notify_seat_pointer_button);
	tw_signal_setup_listener(&seat->source.pointer.motion,
	                         &seat->sink.pointer.motion,
	                         notify_seat_pointer_motion);
	tw_signal_setup_listener(&seat->source.pointer.motion_absolute,
	                         &seat->sink.pointer.motion_absolute,
	                         notify_seat_pointer_motion_abs);
	tw_signal_setup_listener(&seat->source.pointer.axis,
	                         &seat->sink.pointer.axis,
	                         notify_seat_pointer_axis);
	tw_signal_setup_listener(&seat->source.pointer.frame,
	                         &seat->sink.pointer.frame,
	                         notify_seat_pointer_frame);

	tw_signal_setup_listener(&seat->source.pointer.swipe_begin,
	                         &seat->sink.pointer.swipe_begin,
	                         notify_seat_pointer_swipe_begin);
	tw_signal_setup_listener(&seat->source.pointer.swipe_update,
	                         &seat->sink.pointer.swipe_update,
	                         notify_seat_pointer_swipe_update);
	tw_signal_setup_listener(&seat->source.pointer.swipe_end,
	                         &seat->sink.pointer.swipe_end,
	                         notify_seat_pointer_swipe_end);

	tw_signal_setup_listener(&seat->source.pointer.pinch_begin,
	                         &seat->sink.pointer.pinch_begin,
	                         notify_seat_pointer_pinch_begin);
	tw_signal_setup_listener(&seat->source.pointer.pinch_update,
	                         &seat->sink.pointer.pinch_update,
	                         notify_seat_pointer_pinch_update);
	tw_signal_setup_listener(&seat->source.pointer.pinch_end,
	                         &seat->sink.pointer.pinch_end,
	                         notify_seat_pointer_pinch_end);
}

static inline void
seat_install_touch_listeners(struct tw_engine_seat *seat)
{
	tw_signal_setup_listener(&seat->source.touch.down,
	                         &seat->sink.touch.down,
	                         notify_seat_touch_down);
	tw_signal_setup_listener(&seat->source.touch.up,
	                         &seat->sink.touch.up,
	                         notify_seat_touch_up);
	tw_signal_setup_listener(&seat->source.touch.motion,
	                         &seat->sink.touch.motion,
	                         notify_seat_touch_motion);
	tw_signal_setup_listener(&seat->source.touch.cancel,
	                         &seat->sink.touch.cancel,
	                         notify_seat_touch_cancel);
}

static void
seat_install_default_listeners(struct tw_engine_seat *seat)
{
	tw_signal_setup_listener(&seat->tw_seat->signals.focus,
	                         &seat->listeners.focus,
	                         notify_seat_focus_device);
	tw_signal_setup_listener(&seat->tw_seat->signals.unfocus,
	                         &seat->listeners.unfocus,
	                         notify_seat_unfocus_device);
	tw_signal_setup_listener(&seat->source.remove, &seat->sink.remove,
	                         notify_seat_remove_device);
	tw_signal_setup_listener(&seat->source.event, &seat->sink.event,
	                         notify_seat_input_event);
	seat_install_keyboard_listeners(seat);
	seat_install_pointer_listeners(seat);
	seat_install_touch_listeners(seat);
}

static void
seat_add_touch(struct tw_engine_seat *seat, struct tw_input_device *touch)
{
	if (!seat_has_touch(seat->tw_seat))
		tw_seat_new_touch(seat->tw_seat);
}

static void
seat_add_pointer(struct tw_engine_seat *seat, struct tw_input_device *pointer)
{
	if (!seat_has_pointer(seat->tw_seat))
		tw_seat_new_pointer(seat->tw_seat);
}

static void
seat_add_keyboard(struct tw_engine_seat *seat,
                  struct tw_input_device *keyboard)
{
	struct xkb_context *context = seat->engine->xkb_context;

	if (!seat->keymap)
		seat->keymap = xkb_keymap_new_from_names(
			context, &seat->keyboard_rule_names,
			XKB_KEYMAP_COMPILE_NO_FLAGS);
	tw_input_device_set_keymap(keyboard, seat->keymap);

	//setup tw_seat, keymap will provide later.
	if (!seat_has_keyboard(seat->tw_seat))
		tw_seat_new_keyboard(seat->tw_seat);
	//Here we pretty much giveup the keymap directly from backend.
	tw_keyboard_set_keymap(&seat->tw_seat->keyboard, seat->keymap);
}

struct tw_engine_seat *
tw_engine_new_seat(struct tw_engine *engine, unsigned int id)
{
	struct tw_engine_seat *seat;
	char name[32];

	assert(id < 8 && !(engine->seat_pool & (1 << id)));
	seat = &engine->seats[id];
	seat->engine = engine;
	seat->idx = id;

	snprintf(name, sizeof(name), "seat%u", id);
	seat->tw_seat = tw_seat_create(engine->display,
	                               &engine->global_cursor,
	                               name);
	tw_input_source_init(&seat->source);
	wl_list_init(&seat->link);

	engine->seat_pool |= (1 << id);
	wl_list_insert(engine->inputs.prev, &seat->link);

	wl_signal_emit(&engine->signals.seat_created, seat);
	//now we are safely installing the listeners
	seat_install_default_listeners(seat);

	return seat;
}

void
tw_engine_seat_release(struct tw_engine_seat *seat)
{
	uint32_t unset = ~(1 << seat->idx);

	wl_signal_emit(&seat->engine->signals.seat_remove, seat);

	wl_list_remove(&seat->link);
	seat->idx = -1;
	tw_seat_destroy(seat->tw_seat);
	seat->tw_seat = NULL;
	xkb_keymap_unref(seat->keymap);
	seat->keymap = NULL;

	seat->engine->seat_pool &= unset;
}

struct tw_engine_seat *
tw_engine_seat_find_create(struct tw_engine *engine, unsigned int id)
{
	struct tw_engine_seat *seat;
	uint32_t mask = (1 << id);

	assert(id < 8);
	if (mask & engine->seat_pool)
		seat = &engine->seats[id];
	else
		seat = tw_engine_new_seat(engine, id);
	return seat;
}

void
tw_engine_seat_add_input_device(struct tw_engine_seat *seat,
                                struct tw_input_device *device)
{
	switch (device->type) {
	case TW_INPUT_TYPE_KEYBOARD:
		seat_add_keyboard(seat, device);
		break;
	case TW_INPUT_TYPE_POINTER:
		seat_add_pointer(seat, device);
		break;
	case TW_INPUT_TYPE_TOUCH:
		seat_add_touch(seat, device);
		break;
	case TW_INPUT_TYPE_SWITCH:
		break;
	case TW_INPUT_TYPE_TABLET_PAD:
		break;
	case TW_INPUT_TYPE_TABLET_TOOL:
		break;
	}
	tw_input_device_attach_emitter(device, &seat->source);
}

WL_EXPORT void
tw_engine_seat_set_xkb_rules(struct tw_engine_seat *seat,
                             struct xkb_rule_names *rules)
{
	struct tw_engine *engine = seat->engine;
	struct xkb_keymap *keymap;
	struct tw_input_device *dev;

	seat->keyboard_rule_names = *rules;
	if (!seat->keyboard_rule_names.rules)
		seat->keyboard_rule_names.rules = "evdev";
	if (!seat->keyboard_rule_names.model)
		seat->keyboard_rule_names.model = "pc105";
	if (!seat->keyboard_rule_names.layout)
		seat->keyboard_rule_names.layout = "us";
	//if the keyboard has no keymap yet, means they keyboard has not
	//initialized, it is safe to return.
	if (!seat_has_keyboard(seat->tw_seat))
		return;

	keymap = xkb_map_new_from_names(engine->xkb_context, rules,
	                                XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!keymap)
		return;
	wl_list_for_each(dev, &engine->backend->inputs, link)
		tw_input_device_set_keymap(dev, keymap);

	xkb_keymap_unref(seat->keymap);
	seat->keymap = xkb_keymap_ref(keymap);
	xkb_keymap_unref(keymap);
}

WL_EXPORT struct tw_engine_seat *
tw_engine_get_focused_seat(struct tw_engine *engine)
{
	//compare the last serial, the biggest win.
	struct tw_engine_seat *seat, *selected = engine->focused_seat;

	if (!selected)
		wl_list_for_each(seat, &engine->inputs, link) {
			selected = seat;
			break;
		}
	return selected;
}
