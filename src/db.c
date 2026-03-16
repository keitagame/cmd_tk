#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include "tracker.h"

#define DAY_SECS 86400

static int same_day(time_t a, time_t b) {
    struct tm ta, tb;
    localtime_r(&a, &ta);
    localtime_r(&b, &tb);
    return ta.tm_year == tb.tm_year && ta.tm_yday == tb.tm_yday;
}

int db_load(CmdEntry entries[], int *n) {
    *n = 0;
    FILE *fp = fopen(DB_FILE, "rb");
    if (!fp) return 0;
    flock(fileno(fp), LOCK_SH);
    DbHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1 ||
        strcmp(hdr.magic, "CMDTRK") != 0 || hdr.count > MAX_COMMANDS) {
        flock(fileno(fp), LOCK_UN); fclose(fp); return -1;
    }
    for (uint32_t i = 0; i < hdr.count && i < MAX_COMMANDS; i++) {
        if (fread(&entries[i].rec, sizeof(CmdRecord), 1, fp) != 1) break;
        entries[i].rank = (int)i + 1;
        (*n)++;
    }
    flock(fileno(fp), LOCK_UN); fclose(fp);
    return 0;
}

int db_save(CmdEntry entries[], int n) {
    FILE *fp = fopen(DB_FILE, "wb");
    if (!fp) return -1;
    flock(fileno(fp), LOCK_EX);
    DbHeader hdr; memset(&hdr, 0, sizeof(hdr));
    strcpy(hdr.magic, "CMDTRK");
    hdr.version = 2; hdr.count = (uint32_t)n;
    fwrite(&hdr, sizeof(hdr), 1, fp);
    for (int i = 0; i < n; i++)
        fwrite(&entries[i].rec, sizeof(CmdRecord), 1, fp);
    flock(fileno(fp), LOCK_UN); fclose(fp);
    return 0;
}

int db_record(const char *cmd, double elapsed_secs) {
    static CmdEntry entries[MAX_COMMANDS];
    int n = 0;
    db_load(entries, &n);

    int idx = -1;
    for (int i = 0; i < n; i++)
        if (strcmp(entries[i].rec.cmd, cmd) == 0) { idx = i; break; }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    if (idx == -1) {
        if (n >= MAX_COMMANDS) return -1;
        idx = n++;
        memset(&entries[idx], 0, sizeof(CmdEntry));
        strncpy(entries[idx].rec.cmd, cmd, MAX_CMD_LEN - 1);
        entries[idx].rec.first_seen  = now;
        entries[idx].rec.streak      = 1;
        entries[idx].rec.max_streak  = 1;
        entries[idx].rec.active_days = 1;
        entries[idx].rec.last_day    = now;
    } else {
        CmdRecord *r = &entries[idx].rec;
        if (!same_day(r->last_day, now)) {
            double diff = difftime(now, r->last_day);
            if (diff < DAY_SECS * 2) {
                r->streak++;
                if (r->streak > r->max_streak) r->max_streak = r->streak;
            } else {
                r->streak = 1;
            }
            r->active_days++;
            r->last_day = now;
        }
    }

    CmdRecord *r = &entries[idx].rec;
    r->count++;
    r->total_secs += elapsed_secs;
    r->last_seen   = now;
    r->weekday_count[tm_now.tm_wday]++;
    r->hour_count[tm_now.tm_hour]++;

    log_append(cmd, elapsed_secs);
    return db_save(entries, n);
}

/* ── Log ─────────────────────────────────────────────────────── */
int log_append(const char *cmd, double secs) {
    FILE *fp = fopen(LOG_FILE, "ab");
    if (!fp) return -1;
    flock(fileno(fp), LOCK_EX);
    LogEntry e;
    e.ts   = time(NULL);
    e.secs = secs;
    memset(e.cmd, 0, sizeof(e.cmd));
    strncpy(e.cmd, cmd, MAX_CMD_LEN - 1);
    fwrite(&e, sizeof(e), 1, fp);
    flock(fileno(fp), LOCK_UN); fclose(fp);
    return 0;
}

int log_read_recent(LogEntry out[], int max_n, int *got) {
    *got = 0;
    FILE *fp = fopen(LOG_FILE, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long sz      = ftell(fp);
    long n_total = sz / (long)sizeof(LogEntry);
    long start   = (n_total > max_n) ? n_total - max_n : 0;
    fseek(fp, start * (long)sizeof(LogEntry), SEEK_SET);
    *got = (int)fread(out, sizeof(LogEntry), (size_t)max_n, fp);
    fclose(fp);
    return 0;
}

int log_export_csv(const char *path) {
    FILE *in = fopen(LOG_FILE, "rb");
    if (!in) return -1;
    FILE *out = fopen(path, "w");
    if (!out) { fclose(in); return -1; }
    fprintf(out, "timestamp,command,elapsed_secs\n");
    LogEntry e;
    while (fread(&e, sizeof(e), 1, in) == 1) {
        char buf[32];
        fmt_datetime(e.ts, buf, sizeof(buf));
        fprintf(out, "%s,%s,%.3f\n", buf, e.cmd, e.secs);
    }
    fclose(in); fclose(out);
    return 0;
}

/* ── Sort ─────────────────────────────────────────────────────── */
static int cmp_count(const void *a, const void *b) {
    const CmdEntry *A = a, *B = b;
    return (B->rec.count > A->rec.count) - (B->rec.count < A->rec.count);
}
static int cmp_time(const void *a, const void *b) {
    const CmdEntry *A = a, *B = b;
    return (B->rec.total_secs > A->rec.total_secs) - (B->rec.total_secs < A->rec.total_secs);
}
static int cmp_streak(const void *a, const void *b) {
    const CmdEntry *A = a, *B = b;
    return (B->rec.streak > A->rec.streak) - (B->rec.streak < A->rec.streak);
}
static int cmp_days(const void *a, const void *b) {
    const CmdEntry *A = a, *B = b;
    return (B->rec.active_days > A->rec.active_days) - (B->rec.active_days < A->rec.active_days);
}
void db_sort_by_count(CmdEntry e[], int n)  { qsort(e, n, sizeof(CmdEntry), cmp_count);  }
void db_sort_by_time(CmdEntry e[], int n)   { qsort(e, n, sizeof(CmdEntry), cmp_time);   }
void db_sort_by_streak(CmdEntry e[], int n) { qsort(e, n, sizeof(CmdEntry), cmp_streak); }
void db_sort_by_days(CmdEntry e[], int n)   { qsort(e, n, sizeof(CmdEntry), cmp_days);   }

/* ── Formatters ──────────────────────────────────────────────── */
char *fmt_duration(double secs, char *buf, size_t len) {
    uint64_t s = (uint64_t)secs;
    uint64_t h = s / 3600; s %= 3600;
    uint64_t m = s / 60;   s %= 60;
    if (h)      snprintf(buf, len, "%luh%02lum%02lus",
                    (unsigned long)h, (unsigned long)m, (unsigned long)s);
    else if (m) snprintf(buf, len, "%lum%02lus",
                    (unsigned long)m, (unsigned long)s);
    else        snprintf(buf, len, "%.2fs", secs);
    return buf;
}

char *fmt_time(time_t t, char *buf, size_t len) {
    struct tm tm; localtime_r(&t, &tm);
    strftime(buf, len, "%Y-%m-%d", &tm); return buf;
}

char *fmt_datetime(time_t t, char *buf, size_t len) {
    struct tm tm; localtime_r(&t, &tm);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm); return buf;
}

char *fmt_bar(uint64_t val, uint64_t max_val, int width, char *buf) {
    if (max_val == 0) { buf[0] = '\0'; return buf; }
    int filled = (int)((double)val / max_val * width);
    for (int i = 0; i < width; i++) buf[i] = (i < filled) ? '#' : '-';
    buf[width] = '\0'; return buf;
}
