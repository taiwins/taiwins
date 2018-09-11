/*******************************************************************
 *
 * this file servers the purpose of (default)unified configuration
 * shared among server and client
 *
 ******************************************************************/

#ifndef TW_CONFIG_H
#define TW_CONFIG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
/* we define this stride to work with WL_SHM_FORMAT_ARGB888 */
#define DECISION_STRIDE 32
#define NUM_DECISIONS 500

//every decision represents a row in wl_buffer, we need to make it as aligned as possible
struct taiwins_decision_key {
	char app_name[128];
	bool floating;
	int  scale;
} __attribute__ ((aligned (DECISION_STRIDE)));



#ifdef __cplusplus
}
#endif


#endif /* EOF */
