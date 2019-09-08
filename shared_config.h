/*******************************************************************
 *
 * this file servers the purpose of (default)unified configuration
 * shared among server and client
 *
 ******************************************************************/

#ifndef TW_SHARED_CONFIG_H
#define TW_SHARED_CONFIG_H


#include <stdint.h>
#include <stdbool.h>
#include <fontconfig/fontconfig.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wayland-util.h>
#include <sequential.h>

#ifdef __cplusplus
extern "C" {
#endif
/* we define this stride to work with WL_SHM_FORMAT_ARGB888 */
#define DECISION_STRIDE 32
#define NUM_DECISIONS 500


#ifdef __GNUC__
#define DEPRECATED(func) func __attribute__ ((deprecated))
#elif defined(_MSC_VER)
#define DEPRECATED(func) __declspec(deprecated) func
#else
#pragma message("WARNING: You need to implement DEPRECATED for this compiler")
#define DEPRECATED(func) func
#endif

/*****************************************************************/
/*                            theme                              */
/*****************************************************************/


/*****************************************************************/
/*                           console                             */
/*****************************************************************/

struct taiwins_decision_key {
	char app_name[128];
	bool floating;
	int  scale;
} __attribute__ ((aligned (DECISION_STRIDE)));

/*****************************************************************/
/*                            shell                              */
/*****************************************************************/


struct taiwins_window_brief {
	int8_t x,y,w,h;
	const char name[32];
};

#ifdef __cplusplus
}
#endif


#endif /* EOF */
