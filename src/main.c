#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "tracker.h"

#define MAX_ROWS MAX_COMMANDS

static void print_usage(void) {
    puts("cmd_tracker v" VERSION "\n");
    puts("Usage:");
    puts("  cmd_tracker dashboard              Interactive ncurses dashboard");
    puts("  cmd_tracker record <cmd> <secs>    Record a command manually");
    puts("  cmd_tracker stats                  Print full stats table");
    puts("  cmd_tracker top [N]                Show top N commands (default: 10)");
    puts("  cmd_tracker log [N]                Show last N log entries (default: 20)");
    puts("  cmd_tracker export [path]          Export log to CSV");
    puts("  cmd_tracker reset                  Delete all data");
    puts("\nShell integration:");
    puts("  Bash: source /usr/local/share/cmd_tracker/shell_hook.bash");
    puts("  Zsh:  source /usr/local/share/cmd_tracker/shell_hook.zsh");
}

static void cmd_stats(void) {
    CmdEntry entries[MAX_ROWS]; int n = 0;
    db_load(entries, &n);
    if (!n) { puts("No data yet."); return; }
    db_sort_by_count(entries, n);
    printf("\n%-4s  %-20s  %8s  %-12s  %7s  %7s  %s\n",
        "Rank", "Command", "Count", "Total time", "Streak", "Days", "Last used");
    puts("----  --------------------  --------  ------------  -------  -------  ----------");
    for (int i = 0; i < n; i++) {
        CmdRecord *r = &entries[i].rec;
        char t[32], d[16];
        fmt_duration(r->total_secs, t, sizeof(t));
        fmt_time(r->last_seen, d, sizeof(d));
        printf("%-4d  %-20s  %8llu  %-12s  %5llud  %5llud  %s\n",
            i+1, r->cmd,
            (unsigned long long)r->count, t,
            (unsigned long long)r->streak,
            (unsigned long long)r->active_days, d);
    }
    printf("\nTotal: %d command(s)\n", n);
}

static void cmd_top(int top_n) {
    CmdEntry entries[MAX_ROWS]; int n = 0;
    db_load(entries, &n);
    if (!n) { puts("No data yet."); return; }
    db_sort_by_count(entries, n);
    if (top_n > n) top_n = n;
    printf("\n  Top %d commands by usage count\n", top_n);
    printf("  %-3s  %-18s  %8s  %-10s\n", "#", "Command", "Count", "Total time");
    uint64_t mc = entries[0].rec.count;
    if (!mc) mc = 1;
    for (int i = 0; i < top_n; i++) {
        CmdRecord *r = &entries[i].rec;
        char t[32], bar[41];
        fmt_duration(r->total_secs, t, sizeof(t));
        fmt_bar(r->count, mc, 20, bar);
        printf("  %-3d  %-18s  %8llu  %-10s  [%s]\n",
            i+1, r->cmd, (unsigned long long)r->count, t, bar);
    }
}

static void cmd_log(int n) {
    LogEntry *log = (LogEntry *)malloc((size_t)n * sizeof(LogEntry));
    if (!log) { puts("Out of memory."); return; }
    int got = 0;
    log_read_recent(log, n, &got);
    if (!got) { free(log); puts("No log entries yet."); return; }
    printf("\n%-20s  %-18s  %s\n", "Timestamp", "Command", "Duration");
    puts("--------------------  ------------------  ----------");
    for (int i = got-1; i >= 0; i--) {
        char dt[32], t[32];
        fmt_datetime(log[i].ts, dt, sizeof(dt));
        fmt_duration(log[i].secs, t, sizeof(t));
        printf("%-20s  %-18s  %s\n", dt, log[i].cmd, t);
    }
    printf("\n%d entry/entries\n", got);
    free(log);
}

int main(int argc, char *argv[]) {
    if (argc < 2) { print_usage(); return 1; }

    if (!strcmp(argv[1], "dashboard")) {
        return main_dashboard();

    } else if (!strcmp(argv[1], "record")) {
        if (argc < 4) { fputs("Usage: record <cmd> <secs>\n", stderr); return 1; }
        return db_record(argv[2], atof(argv[3])) ? 1 : 0;

    } else if (!strcmp(argv[1], "stats")) {
        cmd_stats(); return 0;

    } else if (!strcmp(argv[1], "top")) {
        cmd_top((argc >= 3) ? atoi(argv[2]) : 10); return 0;

    } else if (!strcmp(argv[1], "log")) {
        cmd_log((argc >= 3) ? atoi(argv[2]) : 20); return 0;

    } else if (!strcmp(argv[1], "export")) {
        const char *path = (argc >= 3) ? argv[2] : "/tmp/cmd_tracker_export.csv";
        if (log_export_csv(path) == 0)
            printf("Exported to: %s\n", path);
        else
            { fputs("Export failed.\n", stderr); return 1; }
        return 0;

    } else if (!strcmp(argv[1], "reset")) {
        printf("Delete all data? [y/N] "); fflush(stdout);
        char ch = (char)getchar();
        if (ch == 'y' || ch == 'Y') {
            remove(DB_FILE); remove(LOG_FILE);
            puts("Data deleted.");
        } else {
            puts("Cancelled.");
        }
        return 0;

    } else {
        print_usage(); return 1;
    }
}
