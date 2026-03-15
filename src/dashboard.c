#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <time.h>
#include <locale.h>
#include <math.h>
#include "tracker.h"

/* ── Colour pairs ─────────────────────────────────────────────── */
#define C_HEADER   1
#define C_TITLE    2
#define C_ODD      3
#define C_EVEN     4
#define C_RANK1    5
#define C_RANK2    6
#define C_RANK3    7
#define C_BAR      8
#define C_BARFG    9
#define C_STREAK   10
#define C_KEY      11
#define C_STATUS   12
#define C_SEL      13
#define C_HEAT0    14
#define C_HEAT1    15
#define C_HEAT2    16
#define C_HEAT3    17
#define C_HEAT4    18
#define C_LOG_CMD  19
#define C_LOG_TIME 20
#define C_POPUP    21
#define C_POPBRD   22
#define C_GRAPH    23

typedef enum {
    SORT_COUNT=0, SORT_TIME, SORT_STREAK, SORT_DAYS, SORT_MAX
} SortMode;

typedef enum {
    VIEW_RANK=0, VIEW_HEAT, VIEW_LOG, VIEW_GRAPH, VIEW_MAX
} ViewMode;

static const char *sort_labels[] = {"回数","時間","連続日","使用日"};
static const char *view_labels[] = {"ランキング","ヒートマップ","ログ","グラフ"};

/* ── State ───────────────────────────────────────────────────── */
#define MAX_ROWS MAX_COMMANDS
#define MAX_LOG  200

static CmdEntry g_entries[MAX_ROWS];
static int      g_n       = 0;
static SortMode g_sort    = SORT_COUNT;
static ViewMode g_view    = VIEW_RANK;
static int      g_sel     = 0;
static int      g_offset  = 0;
static int      g_popup   = 0;   /* detail popup open? */

static LogEntry g_log[MAX_LOG];
static int      g_log_n   = 0;
static int      g_log_off = 0;

/* ── Helpers ─────────────────────────────────────────────────── */
static void load_and_sort(void) {
    g_n = 0;
    db_load(g_entries, &g_n);
    switch (g_sort) {
        case SORT_COUNT:  db_sort_by_count(g_entries,  g_n); break;
        case SORT_TIME:   db_sort_by_time(g_entries,   g_n); break;
        case SORT_STREAK: db_sort_by_streak(g_entries, g_n); break;
        case SORT_DAYS:   db_sort_by_days(g_entries,   g_n); break;
        default: break;
    }
    for (int i = 0; i < g_n; i++) g_entries[i].rank = i + 1;
    log_read_recent(g_log, MAX_LOG, &g_log_n);
}

static void clamp_sel(int visible) {
    if (g_n == 0) { g_sel = 0; return; }
    if (g_sel >= g_n) g_sel = g_n - 1;
    if (g_sel < 0)    g_sel = 0;
    if (g_sel < g_offset)              g_offset = g_sel;
    if (g_sel >= g_offset + visible)   g_offset = g_sel - visible + 1;
}

/* ── Header ──────────────────────────────────────────────────── */
static void draw_header(int cols) {
    attron(COLOR_PAIR(C_HEADER) | A_BOLD);
    mvhline(0, 0, ' ', cols);
    mvprintw(0, 1,
        " CMD TRACKER v" VERSION
        "  |  Linux コマンド使用統計ダッシュボード"
        "  |  コマンド数: %d ", g_n);
    /* current time on right */
    char tbuf[32]; time_t now=time(NULL);
    struct tm tm; localtime_r(&now,&tm);
    strftime(tbuf,sizeof(tbuf),"%H:%M:%S",&tm);
    mvprintw(0, cols-11, " %s ", tbuf);
    attroff(COLOR_PAIR(C_HEADER) | A_BOLD);
}

/* ── Tab bar ─────────────────────────────────────────────────── */
static void draw_tabs(int y, int cols) {
    attron(COLOR_PAIR(C_STATUS));
    mvhline(y, 0, ' ', cols);
    int x = 1;
    for (int i = 0; i < VIEW_MAX; i++) {
        if (i == (int)g_view) {
            attroff(COLOR_PAIR(C_STATUS));
            attron(COLOR_PAIR(C_TITLE) | A_BOLD | A_REVERSE);
        }
        mvprintw(y, x, " [%d] %s ", i+1, view_labels[i]);
        x += (int)strlen(view_labels[i]) + 7;
        if (i == (int)g_view) {
            attroff(COLOR_PAIR(C_TITLE) | A_BOLD | A_REVERSE);
            attron(COLOR_PAIR(C_STATUS));
        }
    }
    attroff(COLOR_PAIR(C_STATUS));
}

/* ── Status bar ──────────────────────────────────────────────── */
static void draw_status(int y, int cols) {
    attron(COLOR_PAIR(C_STATUS));
    mvhline(y, 0, ' ', cols);
    if (g_view == VIEW_RANK) {
        mvprintw(y, 1, "ソート: ");
        attroff(COLOR_PAIR(C_STATUS));
        for (int i = 0; i < SORT_MAX; i++) {
            attron(COLOR_PAIR(C_KEY)|A_BOLD);
            printw("[%c]", 'a'+i);
            attroff(COLOR_PAIR(C_KEY)|A_BOLD);
            attron(COLOR_PAIR(C_STATUS));
            if (i==(int)g_sort) attron(A_UNDERLINE|A_BOLD);
            printw("%s ", sort_labels[i]);
            if (i==(int)g_sort) attroff(A_UNDERLINE|A_BOLD);
            attroff(COLOR_PAIR(C_STATUS));
        }
        attron(COLOR_PAIR(C_STATUS));
    }
    /* universal shortcuts on right */
    mvprintw(y, cols-52,
        "[↑↓]移動 [Enter]詳細 [r]更新 [e]CSV出力 [q]終了");
    attroff(COLOR_PAIR(C_STATUS));
}

/* ═══════════════════════════════════════════════════════════════
   VIEW 1: RANKING TABLE
   ════════════════════════════════════════════════════════════ */
static void draw_rank_header(int y, int cols) {
    attron(COLOR_PAIR(C_TITLE)|A_BOLD);
    mvhline(y, 0, ' ', cols);
    mvprintw(y, 0,
        "%-4s %-18s %8s  %-11s  %5s %5s  %-10s  グラフ",
        " # ", "コマンド", "回数", "合計時間",
        "連続日", "使用日", "最終使用");
    attroff(COLOR_PAIR(C_TITLE)|A_BOLD);
}

static void draw_rank_row(int screen_row, int di, int cols, uint64_t max_cnt) {
    if (di < 0 || di >= g_n) return;
    CmdRecord *r = &g_entries[di].rec;
    int rank = g_entries[di].rank;
    int is_sel = (di == g_sel);
    int pair = is_sel ? C_SEL : ((di%2==0) ? C_ODD : C_EVEN);

    attron(COLOR_PAIR(pair));
    mvhline(screen_row, 0, ' ', cols);

    /* rank badge */
    if (!is_sel) {
        attroff(COLOR_PAIR(pair));
        if      (rank==1) attron(COLOR_PAIR(C_RANK1)|A_BOLD);
        else if (rank==2) attron(COLOR_PAIR(C_RANK2)|A_BOLD);
        else if (rank==3) attron(COLOR_PAIR(C_RANK3)|A_BOLD);
        else              attron(COLOR_PAIR(pair));
    }
    if      (rank==1) mvprintw(screen_row, 0, " #1 ");
    else if (rank==2) mvprintw(screen_row, 0, " #2 ");
    else if (rank==3) mvprintw(screen_row, 0, " #3 ");
    else              mvprintw(screen_row, 0, "%3d ", rank);

    if (!is_sel) {
        attroff(COLOR_PAIR(C_RANK1)|COLOR_PAIR(C_RANK2)|COLOR_PAIR(C_RANK3)|A_BOLD);
        attron(COLOR_PAIR(pair));
    }

    mvprintw(screen_row, 4, "%-18.18s", r->cmd);
    mvprintw(screen_row, 23, "%8llu", (unsigned long long)r->count);

    char tbuf[32]; fmt_duration(r->total_secs, tbuf, sizeof(tbuf));
    mvprintw(screen_row, 33, "%-11s", tbuf);

    /* streak: orange if >=7 */
    if (!is_sel && r->streak >= 7) {
        attroff(COLOR_PAIR(pair));
        attron(COLOR_PAIR(C_STREAK)|A_BOLD);
    }
    mvprintw(screen_row, 46, "%3llud", (unsigned long long)r->streak);
    if (!is_sel && r->streak >= 7) {
        attroff(COLOR_PAIR(C_STREAK)|A_BOLD);
        attron(COLOR_PAIR(pair));
    }

    mvprintw(screen_row, 52, "%3llud", (unsigned long long)r->active_days);

    char dbuf[16]; fmt_time(r->last_seen, dbuf, sizeof(dbuf));
    mvprintw(screen_row, 58, "%-10s", dbuf);

    /* sparkbar */
    int bar_x = 70, bar_w = cols - bar_x - 1;
    if (bar_w > 2) {
        attroff(COLOR_PAIR(pair));
        int filled = (max_cnt>0) ? (int)((double)r->count/max_cnt*bar_w) : 0;
        attron(COLOR_PAIR(C_BAR));
        for (int i=0;i<filled&&i<bar_w;i++) mvaddch(screen_row, bar_x+i, '#');
        attroff(COLOR_PAIR(C_BAR));
        attron(COLOR_PAIR(C_BARFG));
        for (int i=filled;i<bar_w;i++) mvaddch(screen_row, bar_x+i, '-');
        attroff(COLOR_PAIR(C_BARFG));
        attron(COLOR_PAIR(pair));
    }
    attroff(COLOR_PAIR(pair)|A_BOLD);
}

static void draw_rank_view(int y0, int h, int cols) {
    uint64_t max_cnt = 1;
    for (int i=0;i<g_n;i++)
        if (g_entries[i].rec.count > max_cnt) max_cnt = g_entries[i].rec.count;

    int table_rows = h - 1;
    clamp_sel(table_rows);
    draw_rank_header(y0, cols);
    for (int row=0; row<table_rows; row++)
        draw_rank_row(y0+1+row, g_offset+row, cols, max_cnt);
}

/* ═══════════════════════════════════════════════════════════════
   VIEW 2: HEAT MAP  (曜日 × 時間帯)
   ════════════════════════════════════════════════════════════ */
static int heat_pair(uint64_t val, uint64_t mx) {
    if (mx == 0 || val == 0) return C_HEAT0;
    double ratio = (double)val / mx;
    if (ratio < 0.25) return C_HEAT1;
    if (ratio < 0.50) return C_HEAT2;
    if (ratio < 0.75) return C_HEAT3;
    return C_HEAT4;
}

static void draw_heat_view(int y0, int h, int cols) {
    (void)h; (void)cols;
    if (g_n == 0) {
        mvprintw(y0+2, 4, "データがありません");
        return;
    }

    /* Use selected command */
    CmdRecord *r = &g_entries[g_sel].rec;

    attron(COLOR_PAIR(C_TITLE)|A_BOLD);
    mvprintw(y0, 2, "コマンド: %-20s  曜日×時間帯ヒートマップ", r->cmd);
    attroff(COLOR_PAIR(C_TITLE)|A_BOLD);

    /* hour axis: 0..23 in two rows (0-11, 12-23) */
    static const char *days[] = {"日","月","火","水","木","金","土"};

    /* find max in grid */
    uint64_t mx = 1;
    for (int d=0;d<7;d++)
        for (int hh=0;hh<24;hh++) {
            /* approximate: weekday_count[d] * hour_count[hh] / count */
            uint64_t v = 0;
            if (r->count > 0)
                v = (uint64_t)round(
                    (double)r->weekday_count[d] *
                    (double)r->hour_count[hh] / r->count);
            if (v > mx) mx = v;
        }

    /* draw hour header */
    attron(COLOR_PAIR(C_TITLE));
    mvprintw(y0+2, 6, "0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23");
    attroff(COLOR_PAIR(C_TITLE));

    for (int d=0;d<7;d++) {
        int row = y0 + 3 + d;
        mvprintw(row, 2, "%s", days[d]);
        for (int hh=0;hh<24;hh++) {
            uint64_t v = 0;
            if (r->count > 0)
                v = (uint64_t)round(
                    (double)r->weekday_count[d] *
                    (double)r->hour_count[hh] / r->count);
            int col_x = 6 + hh * (hh < 10 ? 2 : (hh < 20 ? 3 : 3));
            /* fixed-width: each cell is 3 chars */
            col_x = 6 + hh * 3;
            int pair = heat_pair(v, mx);
            attron(COLOR_PAIR(pair)|A_BOLD);
            mvprintw(row, col_x, (v==0)?" . ":"[#]");
            attroff(COLOR_PAIR(pair)|A_BOLD);
        }
    }

    /* legend */
    int ly = y0 + 11;
    mvprintw(ly, 2, "凡例: ");
    int pair_list[] = {C_HEAT0,C_HEAT1,C_HEAT2,C_HEAT3,C_HEAT4};
    const char *labels[] = {" . ","[+]","[+]","[#]","[#]"};
    const char *desc[]   = {"なし","少","中","多","最多"};
    int lx = 8;
    for (int i=0;i<5;i++) {
        attron(COLOR_PAIR(pair_list[i])|A_BOLD);
        mvprintw(ly, lx, "%s", labels[i]);
        attroff(COLOR_PAIR(pair_list[i])|A_BOLD);
        mvprintw(ly, lx+3, "%s  ", desc[i]);
        lx += 10;
    }

    /* weekday & hour breakdown bars */
    int by = y0 + 13;
    attron(COLOR_PAIR(C_TITLE)|A_BOLD);
    mvprintw(by, 2, "曜日別使用回数:");
    attroff(COLOR_PAIR(C_TITLE)|A_BOLD);
    uint64_t wmax=1;
    for (int d=0;d<7;d++) if (r->weekday_count[d]>wmax) wmax=r->weekday_count[d];
    for (int d=0;d<7;d++) {
        int filled=(int)((double)r->weekday_count[d]/wmax*20);
        mvprintw(by+1+d, 2, "%s %4llu  ", days[d], (unsigned long long)r->weekday_count[d]);
        attron(COLOR_PAIR(C_BAR));
        for (int i=0;i<filled;i++) mvaddch(by+1+d, 12+i, '#');
        attroff(COLOR_PAIR(C_BAR));
        attron(COLOR_PAIR(C_BARFG));
        for (int i=filled;i<20;i++) mvaddch(by+1+d, 12+i, '-');
        attroff(COLOR_PAIR(C_BARFG));
    }
}

/* ═══════════════════════════════════════════════════════════════
   VIEW 3: LOG
   ════════════════════════════════════════════════════════════ */
static void draw_log_view(int y0, int h, int cols) {
    int visible = h - 1;
    if (g_log_n == 0) {
        mvprintw(y0+2, 4, "ログなし — コマンドを実行すると自動記録されます");
        return;
    }
    attron(COLOR_PAIR(C_TITLE)|A_BOLD);
    mvhline(y0, 0, ' ', cols);
    mvprintw(y0, 1, "%-20s  %-18s  %10s", "タイムスタンプ","コマンド","実行時間");
    attroff(COLOR_PAIR(C_TITLE)|A_BOLD);

    /* newest first */
    if (g_log_off < 0) g_log_off = 0;
    if (g_log_off > g_log_n - visible) g_log_off = g_log_n - visible;
    if (g_log_off < 0) g_log_off = 0;

    for (int row=0; row<visible; row++) {
        int li = g_log_n - 1 - g_log_off - row;
        if (li < 0) break;
        LogEntry *e = &g_log[li];
        char dtbuf[32], tbuf[32];
        fmt_datetime(e->ts, dtbuf, sizeof(dtbuf));
        fmt_duration(e->secs, tbuf, sizeof(tbuf));
        int pair = (row%2==0) ? C_ODD : C_EVEN;
        attron(COLOR_PAIR(pair));
        mvhline(y0+1+row, 0, ' ', cols);
        mvprintw(y0+1+row, 1, "%-20s  ", dtbuf);
        attroff(COLOR_PAIR(pair));
        attron(COLOR_PAIR(C_LOG_CMD)|A_BOLD);
        printw("%-18.18s", e->cmd);
        attroff(COLOR_PAIR(C_LOG_CMD)|A_BOLD);
        attron(COLOR_PAIR(pair));
        printw("  %10s", tbuf);
        attroff(COLOR_PAIR(pair));
    }
}

/* ═══════════════════════════════════════════════════════════════
   VIEW 4: GRAPH (vertical bar chart, top N commands)
   ════════════════════════════════════════════════════════════ */
static void draw_graph_view(int y0, int h, int cols) {
    int chart_h = h - 5;
    if (chart_h < 4) chart_h = 4;

    int n = g_n < 20 ? g_n : 20;
    if (n == 0) {
        mvprintw(y0+2, 4, "データがありません");
        return;
    }

    /* sort copy by count */
    CmdEntry tmp[20];
    memcpy(tmp, g_entries, n * sizeof(CmdEntry));
    db_sort_by_count(tmp, n);

    uint64_t mx = tmp[0].rec.count;
    if (mx == 0) mx = 1;

    /* bar width: fit n bars in (cols-10) */
    int bar_area = cols - 10;
    int bar_w    = bar_area / n;
    if (bar_w < 3) bar_w = 3;
    int total_w  = bar_w * n;
    int x0       = (cols - total_w) / 2;

    attron(COLOR_PAIR(C_TITLE)|A_BOLD);
    mvprintw(y0, 2, "使用回数グラフ  TOP %d コマンド", n);
    attroff(COLOR_PAIR(C_TITLE)|A_BOLD);

    /* y-axis label */
    mvprintw(y0+1, 0, "%6llu", (unsigned long long)mx);
    mvprintw(y0+1+chart_h/2, 0, "%6llu", (unsigned long long)(mx/2));
    mvprintw(y0+1+chart_h, 0, "%6s", "0");

    for (int i=0;i<n;i++) {
        CmdRecord *r = &tmp[i].rec;
        int filled = (int)((double)r->count / mx * chart_h);
        int bx = x0 + i * bar_w;

        /* draw bar (bottom-up) */
        for (int row=0; row<chart_h; row++) {
            int sy = y0 + 1 + chart_h - 1 - row;
            if (row < filled) {
                int pair = (i==0)?C_RANK1:(i==1)?C_RANK2:(i==2)?C_RANK3:C_GRAPH;
                attron(COLOR_PAIR(pair)|A_BOLD);
                for (int bw=0; bw<bar_w-1; bw++) mvaddch(sy, bx+bw, '#');
                attroff(COLOR_PAIR(pair)|A_BOLD);
            } else {
                attron(COLOR_PAIR(C_BARFG));
                for (int bw=0; bw<bar_w-1; bw++) mvaddch(sy, bx+bw, ' ');
                attroff(COLOR_PAIR(C_BARFG));
            }
        }

        /* count above bar */
        mvprintw(y0+1+chart_h-filled-1, bx, "%llu", (unsigned long long)r->count);

        /* label below */
        char lbl[8]; snprintf(lbl, sizeof(lbl), "%-7.7s", r->cmd);
        mvprintw(y0+2+chart_h, bx, "%s", lbl);
    }

    /* baseline */
    mvhline(y0+1+chart_h, x0, ACS_HLINE, total_w);
}

/* ═══════════════════════════════════════════════════════════════
   DETAIL POPUP
   ════════════════════════════════════════════════════════════ */
static void draw_popup(int rows, int cols) {
    if (g_sel < 0 || g_sel >= g_n) return;
    CmdRecord *r = &g_entries[g_sel].rec;

    int pw = 64, ph = 18;
    int px = (cols - pw) / 2;
    int py = (rows - ph) / 2;

    /* shadow */
    attron(COLOR_PAIR(C_EVEN));
    for (int i=0;i<ph;i++) mvhline(py+1+i, px+2, ' ', pw);
    attroff(COLOR_PAIR(C_EVEN));

    /* box */
    attron(COLOR_PAIR(C_POPUP));
    for (int i=0;i<ph;i++) mvhline(py+i, px, ' ', pw);
    /* border */
    attron(COLOR_PAIR(C_POPBRD)|A_BOLD);
    mvhline(py,    px, ACS_HLINE, pw);
    mvhline(py+ph-1, px, ACS_HLINE, pw);
    mvvline(py, px, ACS_VLINE, ph);
    mvvline(py, px+pw-1, ACS_VLINE, ph);
    mvaddch(py,    px,      ACS_ULCORNER);
    mvaddch(py,    px+pw-1, ACS_URCORNER);
    mvaddch(py+ph-1, px,      ACS_LLCORNER);
    mvaddch(py+ph-1, px+pw-1, ACS_LRCORNER);
    attroff(COLOR_PAIR(C_POPBRD)|A_BOLD);

    /* title */
    attron(COLOR_PAIR(C_TITLE)|A_BOLD);
    mvprintw(py, px+2, "[ 詳細: %s ]", r->cmd);
    attroff(COLOR_PAIR(C_TITLE)|A_BOLD);

    attron(COLOR_PAIR(C_POPUP));
    int row = py+2;
    char b1[32],b2[32],b3[32],b4[32];

    mvprintw(row++, px+3, "使用回数     : %llu 回",    (unsigned long long)r->count);
    fmt_duration(r->total_secs, b1, sizeof(b1));
    mvprintw(row++, px+3, "合計実行時間 : %s",          b1);
    if (r->count>0) fmt_duration(r->total_secs/r->count,b2,sizeof(b2));
    else strcpy(b2,"-");
    mvprintw(row++, px+3, "平均実行時間 : %s",          b2);
    row++;
    mvprintw(row++, px+3, "現在連続日数 : %llu 日",     (unsigned long long)r->streak);
    mvprintw(row++, px+3, "最長連続記録 : %llu 日",     (unsigned long long)r->max_streak);
    mvprintw(row++, px+3, "ユニーク日数 : %llu 日",     (unsigned long long)r->active_days);
    row++;
    fmt_time(r->first_seen, b3, sizeof(b3));
    fmt_time(r->last_seen,  b4, sizeof(b4));
    mvprintw(row++, px+3, "初回使用     : %s",          b3);
    mvprintw(row++, px+3, "最終使用     : %s",          b4);
    row++;

    /* peak weekday */
    static const char *days[]={"日","月","火","水","木","金","土"};
    int peak_d=0;
    for (int d=1;d<7;d++) if (r->weekday_count[d]>r->weekday_count[peak_d]) peak_d=d;
    /* peak hour */
    int peak_h=0;
    for (int hh=1;hh<24;hh++) if (r->hour_count[hh]>r->hour_count[peak_h]) peak_h=hh;
    mvprintw(row++, px+3, "最多曜日     : %s曜日 (%llu 回)",
        days[peak_d], (unsigned long long)r->weekday_count[peak_d]);
    mvprintw(row++, px+3, "最多時間帯   : %02d 時台 (%llu 回)",
        peak_h, (unsigned long long)r->hour_count[peak_h]);

    row++;
    attron(COLOR_PAIR(C_KEY)|A_BOLD);
    mvprintw(py+ph-2, px+pw/2-10, "[ Esc / Enter で閉じる ]");
    attroff(COLOR_PAIR(C_KEY)|A_BOLD);

    attroff(COLOR_PAIR(C_POPUP));
}

/* ═══════════════════════════════════════════════════════════════
   MINI FOOTER (selected command info)
   ════════════════════════════════════════════════════════════ */
static void draw_footer(int y, int cols) {
    mvhline(y, 0, ACS_HLINE, cols);
    if (g_sel < 0 || g_sel >= g_n) return;
    CmdRecord *r = &g_entries[g_sel].rec;
    char b1[32],b2[32],b3[32];
    fmt_duration(r->total_secs, b1, sizeof(b1));
    if (r->count>0) fmt_duration(r->total_secs/r->count,b2,sizeof(b2));
    else strcpy(b2,"-");
    fmt_time(r->first_seen, b3, sizeof(b3));
    attron(COLOR_PAIR(C_TITLE));
    mvprintw(y+1, 1,
        "選択: %-12s  回数:%llu  合計:%s  平均:%s  初回:%s  [Enter]詳細",
        r->cmd, (unsigned long long)r->count, b1, b2, b3);
    attroff(COLOR_PAIR(C_TITLE));
    /* pad */
    int cur = getcurx(stdscr);
    for (;cur<cols;cur++) mvaddch(y+1, cur, ' ');
}

/* ═══════════════════════════════════════════════════════════════
   MAIN LOOP
   ════════════════════════════════════════════════════════════ */
int main_dashboard(void) {
    setlocale(LC_ALL, "");
    initscr(); cbreak(); noecho();
    keypad(stdscr, TRUE); curs_set(0);
    timeout(3000);

    start_color(); use_default_colors();

    init_pair(C_HEADER,  COLOR_WHITE,   COLOR_BLUE);
    init_pair(C_TITLE,   COLOR_CYAN,    -1);
    init_pair(C_ODD,     -1,            -1);
    init_pair(C_EVEN,    COLOR_WHITE,   -1);
    init_pair(C_RANK1,   COLOR_YELLOW,  -1);
    init_pair(C_RANK2,   -1,            -1);
    init_pair(C_RANK3,   COLOR_RED,     -1);
    init_pair(C_BAR,     COLOR_GREEN,   -1);
    init_pair(C_BARFG,   COLOR_BLACK,   -1);
    init_pair(C_STREAK,  COLOR_YELLOW,  -1);
    init_pair(C_KEY,     COLOR_CYAN,    -1);
    init_pair(C_STATUS,  COLOR_BLACK,   COLOR_CYAN);
    init_pair(C_SEL,     COLOR_BLACK,   COLOR_WHITE);
    /* heat map colours */
    init_pair(C_HEAT0,   COLOR_BLACK,   COLOR_BLACK);
    init_pair(C_HEAT1,   COLOR_GREEN,   -1);
    init_pair(C_HEAT2,   COLOR_YELLOW,  -1);
    init_pair(C_HEAT3,   COLOR_RED,     -1);
    init_pair(C_HEAT4,   COLOR_WHITE,   COLOR_RED);
    /* log colours */
    init_pair(C_LOG_CMD,  COLOR_CYAN,   -1);
    init_pair(C_LOG_TIME, COLOR_YELLOW, -1);
    /* popup colours */
    init_pair(C_POPUP,   COLOR_WHITE,   COLOR_BLUE);
    init_pair(C_POPBRD,  COLOR_CYAN,    COLOR_BLUE);
    /* graph */
    init_pair(C_GRAPH,   COLOR_GREEN,   -1);

    load_and_sort();

    while (1) {
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        if (cols < 80 || rows < 12) {
            clear();
            mvprintw(rows/2, 0, "ターミナルを広くしてください (80x12 以上)");
            refresh();
            if (getch()=='q') break;
            continue;
        }

        /* Layout: header(1) + tabs(1) + content + footer(2) + status(1) */
        int content_y = 2;
        int content_h = rows - content_y - 2 - 1;

        clear();
        draw_header(cols);
        draw_tabs(1, cols);

        switch (g_view) {
            case VIEW_RANK:
                draw_rank_view(content_y, content_h, cols);
                break;
            case VIEW_HEAT:
                draw_heat_view(content_y, content_h, cols);
                break;
            case VIEW_LOG:
                draw_log_view(content_y, content_h, cols);
                break;
            case VIEW_GRAPH:
                draw_graph_view(content_y, content_h, cols);
                break;
            default: break;
        }

        draw_footer(rows-3, cols);
        draw_status(rows-1, cols);

        if (g_popup) draw_popup(rows, cols);

        refresh();

        int ch = getch();

        if (g_popup) {
            /* popup consumes all keys */
            if (ch==27 || ch=='\n' || ch==KEY_ENTER) g_popup = 0;
            continue;
        }

        switch (ch) {
            case 'q': case 'Q': goto done;

            /* view switch */
            case '1': g_view = VIEW_RANK;  break;
            case '2': g_view = VIEW_HEAT;  break;
            case '3': g_view = VIEW_LOG;   break;
            case '4': g_view = VIEW_GRAPH; break;

            /* sort (in rank view) */
            case 'a': g_sort = SORT_COUNT;  load_and_sort(); break;
            case 'b': g_sort = SORT_TIME;   load_and_sort(); break;
            case 'c': g_sort = SORT_STREAK; load_and_sort(); break;
            case 'd': g_sort = SORT_DAYS;   load_and_sort(); break;

            case 'r': case 'R': load_and_sort(); break;

            /* navigation */
            case KEY_UP:
                if (g_view==VIEW_LOG) { g_log_off++; }
                else { if (g_sel>0) g_sel--; }
                break;
            case KEY_DOWN:
                if (g_view==VIEW_LOG) { if (g_log_off>0) g_log_off--; }
                else { if (g_sel<g_n-1) g_sel++; }
                break;
            case KEY_PPAGE:
                if (g_view==VIEW_LOG) g_log_off += 10;
                else { g_sel -= (rows-6); if (g_sel<0) g_sel=0; }
                break;
            case KEY_NPAGE:
                if (g_view==VIEW_LOG) { g_log_off-=10; if(g_log_off<0)g_log_off=0; }
                else { g_sel += (rows-6); if (g_sel>=g_n) g_sel=g_n-1; }
                break;

            /* detail popup */
            case '\n': case KEY_ENTER:
                if (g_n > 0) g_popup = 1;
                break;

            /* CSV export */
            case 'e': case 'E': {
                char path[128];
                snprintf(path, sizeof(path), "/tmp/cmd_tracker_export.csv");
                endwin();
                if (log_export_csv(path)==0)
                    printf("CSV出力完了: %s\n", path);
                else
                    printf("CSV出力失敗\n");
                printf("Enterキーで戻る...");
                fflush(stdout);
                getchar();
                initscr(); cbreak(); noecho();
                keypad(stdscr,TRUE); curs_set(0);
                timeout(3000);
                break;
            }

            case ERR:
                /* auto-refresh */
                load_and_sort();
                break;
        }
    }
done:
    endwin();
    return 0;
}