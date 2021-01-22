#ifndef __TEST_DESKTOP_H
#define __TEST_DESKTOP_H

#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/layers.h>
#include <taiwins/objects/desktop.h>
#include <taiwins/engine.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * the test desktop implements a rather simple stacking desktop API, it is
 * shared among testers
 */
struct tw_test_desktop {
	struct wl_display *display;
	struct tw_engine *engine;
	struct tw_desktop_manager manager;

	struct tw_layer layer;
};

void
tw_test_desktop_init(struct tw_test_desktop *desktop,
                     struct tw_engine *engine);
void
tw_test_desktop_fini(struct tw_test_desktop *desktop);


#ifdef __cplusplus
}
#endif


#endif /* EOF */
