#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "tracker.h"

#define MAX_ROWS MAX_COMMANDS

static void print_usage(void) {
    puts("cmd_tracker v" VERSION "\n");
    puts("使い方:");
    puts("  cmd_tracker dashboard              ダッシュボード (ncurses)");
    puts("  cmd_tracker record <cmd> <secs>    コマンドを手動記録");
    puts("  cmd_tracker stats                  テキスト一覧");
    puts("  cmd_tracker top [N]                上位N件 (既定:10)");
    puts("  cmd_tracker log [N]                実行ログ N件 (既定:20)");
    puts("  cmd_tracker export [path]          ログをCSV出力");
    puts("  cmd_tracker reset                  データ全削除");
    puts("\nシェル統合:");
    puts("  Bash: source /usr/local/share/cmd_tracker/shell_hook.bash");
    puts("  Zsh:  source /usr/local/share/cmd_tracker/shell_hook.zsh");
}

static void cmd_stats(void) {
    CmdEntry entries[MAX_ROWS]; int n=0;
    db_load(entries, &n);
    if (!n) { puts("データなし"); return; }
    db_sort_by_count(entries, n);
    printf("\n%-4s  %-20s  %8s  %-12s  %6s  %6s  %s\n",
        "順位","コマンド","回数","合計時間","連続日","使用日","最終使用");
    puts("----  --------------------  --------  ------------  ------  ------  ----------");
    for (int i=0;i<n;i++) {
        CmdRecord *r=&entries[i].rec;
        char t[32],d[16];
        fmt_duration(r->total_secs,t,sizeof(t));
        fmt_time(r->last_seen,d,sizeof(d));
        printf("%-4d  %-20s  %8llu  %-12s  %4llud  %4llud  %s\n",
            i+1, r->cmd,
            (unsigned long long)r->count, t,
            (unsigned long long)r->streak,
            (unsigned long long)r->active_days, d);
    }
    printf("\n合計 %d コマンド\n", n);
}

static void cmd_top(int top_n) {
    CmdEntry entries[MAX_ROWS]; int n=0;
    db_load(entries, &n);
    if (!n) { puts("データなし"); return; }
    db_sort_by_count(entries, n);
    if (top_n > n) top_n = n;
    printf("\n  使用回数ランキング TOP %d\n", top_n);
    printf("  %-3s  %-18s  %8s  %-10s\n", "#","コマンド","回数","合計時間");
    uint64_t mc = entries[0].rec.count;
    if (!mc) mc = 1;
    for (int i=0;i<top_n;i++) {
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
    if (!log) { puts("メモリ不足"); return; }
    int got = 0;
    log_read_recent(log, n, &got);
    if (!got) { free(log); puts("ログなし"); return; }
    printf("\n%-20s  %-18s  %s\n", "タイムスタンプ","コマンド","実行時間");
    puts("--------------------  ------------------  ----------");
    for (int i = got-1; i >= 0; i--) {
        char dt[32], t[32];
        fmt_datetime(log[i].ts, dt, sizeof(dt));
        fmt_duration(log[i].secs, t, sizeof(t));
        printf("%-20s  %-18s  %s\n", dt, log[i].cmd, t);
    }
    printf("\n%d件\n", got);
    free(log);
}

int main(int argc, char *argv[]) {
    if (argc < 2) { print_usage(); return 1; }

    if (!strcmp(argv[1],"dashboard")) {
        return main_dashboard();

    } else if (!strcmp(argv[1],"record")) {
        if (argc < 4) { fputs("使い方: record <cmd> <secs>\n", stderr); return 1; }
        return db_record(argv[2], atof(argv[3])) ? 1 : 0;

    } else if (!strcmp(argv[1],"stats")) {
        cmd_stats(); return 0;

    } else if (!strcmp(argv[1],"top")) {
        cmd_top((argc>=3) ? atoi(argv[2]) : 10); return 0;

    } else if (!strcmp(argv[1],"log")) {
        cmd_log((argc>=3) ? atoi(argv[2]) : 20); return 0;

    } else if (!strcmp(argv[1],"export")) {
        const char *path = (argc>=3) ? argv[2] : "/tmp/cmd_tracker_export.csv";
        if (log_export_csv(path) == 0)
            printf("CSV出力: %s\n", path);
        else
            { fputs("出力失敗\n", stderr); return 1; }
        return 0;

    } else if (!strcmp(argv[1],"reset")) {
        printf("本当に削除しますか？ [y/N] "); fflush(stdout);
        char ch = (char)getchar();
        if (ch=='y' || ch=='Y') { remove(DB_FILE); remove(LOG_FILE); puts("削除完了"); }
        else puts("キャンセル");
        return 0;

    } else {
        print_usage(); return 1;
    }
}