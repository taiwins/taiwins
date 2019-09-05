#ifndef TW_WESTON_PRIVATE_H
#define TW_WESTON_PRIVATE_H

#if defined (LIBWESTON_7)
#include <libweston/libweston.h>
#include <libweston-desktop/libweston-desktop.h>

#if defined (INCLUDE_BACKEND)
#include <libweston/backend-drm.h>
#include <libweston/backend-wayland.h>
#include <libweston/backend-x11.h>
#endif

#else
#include <compositor.h>

#if defined (INCLUDE_BACKEND)
#include <compositor-drm.h>
#include <compositor-wayland.h>
#include <compositor-x11.h>
#endif


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
