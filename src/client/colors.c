#include "colors.h"

#include <unistd.h>

int g_colors_enabled = 0;

void colors_init(void)
{
    g_colors_enabled = isatty(STDOUT_FILENO) ? 1 : 0;
}
