#ifndef CLI_H
#define CLI_H

#include <sf/str.h>

#define TUI_UL  "\x1b[4m"
#define TUI_BLD "\x1b[1m"
#define TUI_ERR "\x1b[1;31m"
#define TUI_CLR "\x1b[0m"

int sol_cli_cbg(char *path, sf_str src);
void cli_highlight_line(sf_str src, sf_str err, uint16_t line, uint16_t column);

#endif // CLI_H
