#ifndef TW_BACKEND_H
#define TW_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif
struct weston_compositor;
struct taiwins_config;

bool
tw_setup_backend(struct weston_compositor *ec, struct taiwins_config *c);

#ifdef __cplusplus
}
#endif


#endif
