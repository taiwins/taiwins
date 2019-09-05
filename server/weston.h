#ifndef TW_WESTON_PRIVATE_H
#define TW_WESTON_PRIVATE_H

#include <libweston/libweston.h>

#if defined (INCLUDE_DESKTOP)
#include <libweston-desktop/libweston-desktop.h>
#endif

#if defined (INCLUDE_BACKEND)
#include <libweston/backend-drm.h>
#include <libweston/backend-wayland.h>
#include <libweston/backend-x11.h>
#include <libweston/windowed-output-api.h>
#endif

/* this file contains many functions which will become private in the incoming
 * weston release. We have them here to provide to proviate functionanities for
 * taiwins */

#ifdef  __cplusplus
extern "C" {
#endif


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
