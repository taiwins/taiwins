#ifndef TW_DESKTOP_H
#define TW_DESKTOP_H

#include "shell.h"

#ifdef  __cplusplus
extern "C" {
#endif


struct desktop;
struct console;

struct console *announce_console(struct weston_compositor *compositor,
				       struct shell *shell, const char *exec_path);

struct desktop *announce_desktop(struct weston_compositor *compositor);
void end_desktop(struct desktop *desktop);



#ifdef  __cplusplus
}
#endif

#endif /* EOF */
