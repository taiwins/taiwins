#ifndef TW_BACKEND_H
#define TW_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif
struct tw_backend;

bool
tw_setup_backend(struct weston_compositor *compositor);

struct tw_backend*
tw_get_backend(void);

#ifdef __cplusplus
}
#endif


#endif
