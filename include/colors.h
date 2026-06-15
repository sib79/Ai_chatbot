#ifndef COLORS_H
#define COLORS_H

#include <stdio.h>

extern int g_colors_enabled;

void colors_init(void);

#define C_RESET   (g_colors_enabled ? "\033[0m" : "")
#define C_BOLD    (g_colors_enabled ? "\033[1m" : "")
#define C_GREEN   (g_colors_enabled ? "\033[32m" : "")
#define C_CYAN    (g_colors_enabled ? "\033[36m" : "")
#define C_YELLOW  (g_colors_enabled ? "\033[33m" : "")
#define C_RED     (g_colors_enabled ? "\033[31m" : "")
#define C_MAGENTA (g_colors_enabled ? "\033[35m" : "")

#endif /* COLORS_H */
