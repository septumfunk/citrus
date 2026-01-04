#include "sol/bytecode.h"
#include "sol/vm.h"
#include "sol/cli.h"
#include <limits.h>
#include <ctype.h>
#include <ncurses.h>
#include <sf/str.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMD_MAX 256

typedef struct {
    sf_str src;
    sol_fproto proto;
    WINDOW *src_w, *asm_w, *cmd_w;
    char cmd[CMD_MAX];
    bool *bp, e, cap_cur;
    int line_o, line_n, cmd_len, cur, src_h;
} sol_debugger;
static sol_debugger dbg;

static inline void sol_cmdc(void) {
    dbg.e = false;
    memset(dbg.cmd, 0, CMD_MAX);
    dbg.cmd_len = 0;
}

static inline void sol_cmderr(char *s, int len) {
    memset(dbg.cmd, 0, CMD_MAX);
    memcpy(dbg.cmd, s, len);
    dbg.cmd_len = len;
    dbg.e = true;
}

static int sol_pcmd(void) {
    if (sf_str_eq(sf_ref(dbg.cmd), sf_lit("q")))
        return -1;
    if (sf_str_eq(sf_ref(dbg.cmd), sf_lit("b")) && dbg.bp) {
        dbg.bp[dbg.cur - 1] = !dbg.bp[dbg.cur - 1];
        sol_cmdc();
        return 0;
    }
    if (sf_str_eq(sf_ref(dbg.cmd), sf_lit("cm"))) {
        dbg.cap_cur = !dbg.cap_cur;
        sol_cmderr((dbg.cap_cur ? "Cursor cap ON " : "Cursor cap OFF"), 14);
        return 0;
    }

    sf_str f = sf_str_fmt("Unknown Command: %s", dbg.cmd);
    sol_cmderr(f.c_str, (int)f.len);
    return 0;
}

#define fmt dbg.bp && dbg.bp[dbg.line_n - 1] ? (dbg.cur == dbg.line_n ? "o%4u > %.*s\n" : "o%4u | %.*s\n") : \
                                            (dbg.cur == dbg.line_n ? " %4u > %.*s\n" : " %4u | %.*s\n")

static void sol_dsrc(void) {
    werase(dbg.src_w);
    wmove(dbg.src_w, 1, 0);
    dbg.line_n = 1;
    char *cs, *c = dbg.src.c_str, *end = dbg.src.c_str + dbg.src.len;
    cs = c;
    while (c < end) {
        if (*c == '\n') {
            if (dbg.line_n > dbg.line_o - 1)
                mvwprintw(dbg.src_w, dbg.line_n - (dbg.line_o - 1), 2, fmt, dbg.line_n, (int)(c - cs), cs);
            cs = c + 1;
            dbg.line_n++;
        }
        c++;
    }
    if (cs < end && dbg.line_n > dbg.line_o - 1)
        mvwprintw(dbg.src_w, dbg.line_n - (dbg.line_o - 1), 2, fmt, dbg.line_n, (int)(c - cs), cs);

    if (!dbg.bp)
        dbg.bp = calloc((size_t)dbg.line_n, sizeof(bool));

    box(dbg.src_w, 0, 0);
    wrefresh(dbg.src_w);
}

static void sol_dasm(void) {
    werase(dbg.asm_w);

    sol_dbg *db = dbg.proto.dbg,
            *end = dbg.proto.dbg + dbg.proto.code_s;
    if (!db) {
        mvwprintw(dbg.asm_w, 1, 1, "Assembly unavailable.");
        box(dbg.asm_w, 0, 0);
        wrefresh(dbg.asm_w);
        return;
    }

    sol_dbg *cur_db = db;  // start with first entry
    for (sol_dbg *it = db; it < end; ++it) {
        int line = (int)SOL_DBG_LINE(*it);
        if (line <= dbg.cur) {
            if ((int)SOL_DBG_LINE(*cur_db) < line)
                cur_db = it;
        } else
            break;
    }


    int y = 1;
    for (sol_dbg *it = cur_db; it < end && ((int)SOL_DBG_LINE(*it) <= dbg.cur || !dbg.cap_cur); ++it, ++y) {
        sol_instruction ins = dbg.proto.code[it - dbg.proto.dbg];
        const char *op = sol_op_info(sol_ins_op(ins))->mnemonic;
        uint16_t line = SOL_DBG_LINE(*it), column = SOL_DBG_COL(*it);
        switch (sol_op_info(sol_ins_op(ins))->type) {
            case SOL_INS_A: mvwprintw(dbg.asm_w, y, 1, "%4u:%-3u %-7s %-8d", line, column, op, sol_ia_a(ins)); break;
            case SOL_INS_AB: mvwprintw(dbg.asm_w, y, 1, "%4u:%-3u %-7s %-4u %-4u", line, column, op, sol_iab_a(ins), sol_iab_b(ins)); break;
            case SOL_INS_ABC: mvwprintw(dbg.asm_w, y, 1, "%4u:%-3u %-7s %-4u %-4u %-4u", line, column, op, sol_iabc_a(ins), sol_iabc_b(ins), sol_iabc_c(ins)); break;
        }
    }

    box(dbg.asm_w, 0, 0);
    wrefresh(dbg.asm_w);
}

static void sol_dcmd(void) {
    nodelay(dbg.cmd_w, FALSE);
    keypad(dbg.cmd_w, true);

    int ch = 0;
    int prev_lines = 0, prev_cols = 0;
    getmaxyx(stdscr, prev_lines, prev_cols);
    while (1) {
        int new_lines, new_cols;
        getmaxyx(stdscr, new_lines, new_cols);
        if (new_lines != prev_lines || new_cols != prev_cols) {
            resize_term(new_lines, new_cols);
            clear();

            wresize(dbg.src_w, new_lines - 3, new_cols / 2);
            mvwin(dbg.src_w, 0, 0);
            wresize(dbg.asm_w, new_lines - 3, new_cols - new_cols / 2);
            mvwin(dbg.asm_w, 0, new_cols / 2);
            wresize(dbg.cmd_w, 3, new_cols);
            mvwin(dbg.cmd_w, new_lines - 3, 0);

            touchwin(dbg.src_w);
            touchwin(dbg.asm_w);
            touchwin(dbg.cmd_w);

            dbg.src_h = new_lines - 5;
            prev_lines = new_lines;
            prev_cols = new_cols;
        }

        werase(dbg.cmd_w);
        box(dbg.cmd_w, 0, 0);

        sol_dsrc();
        sol_dasm();

        mvwprintw(dbg.cmd_w, 1, 2, "> %.*s", dbg.cmd_len, dbg.cmd);
        wmove(dbg.cmd_w, 1, 4 + dbg.cmd_len);
        wrefresh(dbg.cmd_w);

        ch = wgetch(dbg.cmd_w);
        if (dbg.e)
            sol_cmdc();

        if (ch == '\n') {
            if (dbg.cmd_len == 0)
                continue;
            dbg.cmd[dbg.cmd_len] = '\0';
            if (sol_pcmd() == -1)
                break;
        } else if (ch == KEY_BACKSPACE || ch == 127) {
            if (dbg.cmd_len > 0) dbg.cmd_len--;
        } else if (ch == KEY_UP) {
            if (dbg.cur > 1) dbg.cur -= 1;
            if (dbg.cur < dbg.line_o)
                --dbg.line_o;
        } else if (ch == KEY_DOWN) {
            if (dbg.cur < dbg.line_n) dbg.cur += 1;
            if (dbg.cur - (dbg.line_o - 1) > dbg.src_h)
                ++dbg.line_o;
        } else if (isprint(ch) && dbg.cmd_len < CMD_MAX - 1) {
            dbg.cmd[dbg.cmd_len++] = (char)ch;
        }
    }
}

int sol_cli_cbg(char *path, sf_str src) {
#ifndef _WIN32
    (void)path;

    // Compile
    sol_state *s = sol_state_new();
    sol_usestd(s);
    sol_compile_ex comp_ex = sol_compile(s, src);
    if (!comp_ex.is_ok) {
        fprintf(stderr, TUI_ERR "error: %s:%u:%u\n" TUI_CLR, path, comp_ex.err.line, comp_ex.err.column);
        cli_highlight_line(src, sol_err_string(comp_ex.err.tt), comp_ex.err.line, comp_ex.err.column);
        sol_state_free(s);
        return -1;
    }
    sol_state_free(s);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    int line, col, w;
    getmaxyx(stdscr, line, col);
    w = col / 2;
    dbg = (sol_debugger){
        .src = src, .proto = comp_ex.ok,
        .src_w = newwin(line - 3, w, 0, 0),
        .asm_w = newwin(line - 3, col - w, 0, w),
        .cmd_w = newwin(3, col, line - 3, 0),
        .cap_cur = true, .e = false,
        .line_o = 1, .line_n = 1, .cmd_len = 0, .cur = 1,
        .src_h = line - 5,
    };
    if (!dbg.src_w || !dbg.asm_w || !dbg.cmd_w) return -1;

    sol_dcmd();

    delwin(dbg.src_w);
    delwin(dbg.asm_w);
    delwin(dbg.cmd_w);
    free(dbg.bp);
    sol_fproto_free(&dbg.proto);
    endwin();
    return 0;
#else
    (void)path; (void)src;
    fprintf(stderr, TUI_ERR "Unimplemented\n" TUI_CLR);
    return 0;
#endif
}
