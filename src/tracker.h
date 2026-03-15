#ifndef TRACKER_H
#define TRACKER_H

#include <time.h>
#include <stdint.h>

#define MAX_CMD_LEN     128
#define MAX_COMMANDS    1024
#define MAX_LOG_ENTRIES 65536
#define DB_FILE         "/tmp/cmd_tracker.db"
#define LOG_FILE        "/tmp/cmd_tracker.log"
#define VERSION         "1.1.0"

/* On-disk record for each command entry */
typedef struct {
    char     cmd[MAX_CMD_LEN];
    uint64_t count;
    double   total_secs;
    uint64_t streak;
    uint64_t max_streak;
    uint64_t active_days;
    time_t   first_seen;
    time_t   last_seen;
    time_t   last_day;
    uint64_t weekday_count[7];   /* 0=Sun..6=Sat */
    uint64_t hour_count[24];     /* 0..23        */
    uint8_t  _pad[64];
} CmdRecord;

typedef struct {
    CmdRecord rec;
    int       rank;
} CmdEntry;

typedef struct {
    char     magic[8];
    uint32_t version;
    uint32_t count;
} DbHeader;

typedef struct {
    time_t   ts;
    double   secs;
    char     cmd[MAX_CMD_LEN];
} LogEntry;

int  db_load(CmdEntry entries[], int *n);
int  db_save(CmdEntry entries[], int n);
int  db_record(const char *cmd, double elapsed_secs);
void db_sort_by_count(CmdEntry entries[], int n);
void db_sort_by_time(CmdEntry entries[], int n);
void db_sort_by_streak(CmdEntry entries[], int n);
void db_sort_by_days(CmdEntry entries[], int n);

int  log_append(const char *cmd, double secs);
int  log_read_recent(LogEntry out[], int max_n, int *got);
int  log_export_csv(const char *path);

char *fmt_duration(double secs, char *buf, size_t len);
char *fmt_time(time_t t, char *buf, size_t len);
char *fmt_datetime(time_t t, char *buf, size_t len);
char *fmt_bar(uint64_t val, uint64_t max_val, int width, char *buf);

int main_dashboard(void);

#endif