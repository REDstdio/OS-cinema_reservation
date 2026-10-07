#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <mqueue.h>
#include "common.h"

/* ---------- Shared Data ----------
 * seats[i] = 0  -> AVAILABLE
 * seats[i] = N  -> จองโดย Client-N
 * ทุก Worker Thread เข้าถึง array นี้ร่วมกัน  */
static int seats[NUM_SEATS + 1];
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER; /* ป้องกัน seats[] */
static pthread_mutex_t log_lock   = PTHREAD_MUTEX_INITIALIZER; /* ป้องกันไม่ให้ log ปนกัน */

static long log_seq = 0;
static int use_sync = 1, use_delay = 1, num_workers = 3;
static volatile sig_atomic_t running = 1;
static mqd_t req_q;

static void slog(int wid, const char *fmt, ...)
{
    char msg[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);

    pthread_mutex_lock(&log_lock);
    long n = ++log_seq;
    if (wid > 0)
        printf("#%04ld %02d:%02d:%02d.%03ld [Worker-%d] %s\n", n, tm.tm_hour, tm.tm_min,
               tm.tm_sec, ts.tv_nsec / 1000000, wid, msg);
    else
        printf("#%04ld %02d:%02d:%02d.%03ld [Server  ] %s\n", n, tm.tm_hour, tm.tm_min,
               tm.tm_sec, ts.tv_nsec / 1000000, msg);
    fflush(stdout);
    pthread_mutex_unlock(&log_lock);
}

/* ---------- Critical Section helpers ---------- */
static void enter_cs(int wid)
{
    if (use_sync) pthread_mutex_lock(&table_lock);
    slog(wid, "entering critical section%s", use_sync ? "" : " (NO LOCK!)");
}

static void leave_cs(int wid)
{
    slog(wid, "leaving critical section");
    if (use_sync) pthread_mutex_unlock(&table_lock);
}

/* random delay 50-500 ms  ใช้ขยาย Race Window (เพื่อการทดลองเท่านั้น) */
static void random_delay(int wid, unsigned *seed)
{
    if (!use_delay) return;
    int ms = 50 + rand_r(seed) % 451;
    slog(wid, "random delay %d ms (between check and update)", ms);
    usleep(ms * 1000);
}

/* ---------- Command handlers ---------- */
static void do_list(char *out, size_t sz)
{
    size_t off = 0;
    off += snprintf(out + off, sz - off, "Cinema seats (freeN = available, C<n> = reserved by Client-n)\n");
    for (int i = 1; i <= NUM_SEATS && off < sz; i++) {
        if (seats[i] == 0)
            off += snprintf(out + off, sz - off, "[%2d: free] ", i);
        else
            off += snprintf(out + off, sz - off, "[%2d: C%-3d] ", i, seats[i]);
        if (i % 5 == 0) off += snprintf(out + off, sz - off, "\n");
    }
}

static void handle(int wid, unsigned *seed, const request_t *rq, reply_t *rp)
{
    const char *cmd = rq->cmd;
    int id = rq->resource_id, cid = rq->client_id;
    rp->ok = 0;
    rp->text[0] = '\0';

    if (!strcmp(cmd, "LIST")) {
        enter_cs(wid);
        do_list(rp->text, sizeof rp->text);
        rp->ok = 1;
        leave_cs(wid);
        return;
    }
    if (!strcmp(cmd, "QUIT")) {
        rp->ok = 1;
        snprintf(rp->text, sizeof rp->text, "Goodbye Client-%d", cid);
        return;
    }
    if (!strcmp(cmd, "RESET")) { /* คำสั่งเสริมสำหรับการทดลอง: ล้างการจองทั้งหมด */
        enter_cs(wid);
        memset(seats, 0, sizeof seats);
        rp->ok = 1;
        snprintf(rp->text, sizeof rp->text, "All seats reset to AVAILABLE");
        leave_cs(wid);
        return;
    }

    if (strcmp(cmd, "STATUS") && strcmp(cmd, "RESERVE") && strcmp(cmd, "CANCEL")) {
        snprintf(rp->text, sizeof rp->text, "FAILED: unknown command '%s'", cmd);
        return;
    }
    if (id < 1 || id > NUM_SEATS) {
        snprintf(rp->text, sizeof rp->text, "FAILED: seat must be 1-%d", NUM_SEATS);
        return;
    }

    if (!strcmp(cmd, "STATUS")) {
        enter_cs(wid);
        int owner = seats[id];
        rp->ok = 1;
        if (owner == 0) snprintf(rp->text, sizeof rp->text, "Seat %d: AVAILABLE", id);
        else            snprintf(rp->text, sizeof rp->text, "Seat %d: RESERVED by Client-%d", id, owner);
        leave_cs(wid);
    }
    else if (!strcmp(cmd, "RESERVE")) {
        enter_cs(wid);
        int owner = seats[id];                                   /* CHECK */
        slog(wid, "check Resource %d: %s", id, owner == 0 ? "AVAILABLE" : "RESERVED");
        if (owner == 0) {
            random_delay(wid, seed);                             /* ขยาย race window */
            if (seats[id] != 0)                                  /* ตรวจจับ race เพื่อเป็นหลักฐาน */
                slog(wid, "!!! RACE CONDITION DETECTED: Resource %d already owned by Client-%d, "
                          "but Client-%d overwrites it", id, seats[id], cid);
            seats[id] = cid;                                     /* UPDATE */
            slog(wid, "Resource %d reserved by Client-%d", id, cid);
            rp->ok = 1;
            snprintf(rp->text, sizeof rp->text, "SUCCESS: seat %d reserved by Client-%d", id, cid);
        } else {
            slog(wid, "Resource %d already reserved", id);
            snprintf(rp->text, sizeof rp->text, "FAILED: seat %d already reserved by Client-%d", id, owner);
        }
        leave_cs(wid);
    }
    else if (!strcmp(cmd, "CANCEL")) {
        enter_cs(wid);
        int owner = seats[id];
        slog(wid, "check Resource %d: %s", id, owner == 0 ? "AVAILABLE" : "RESERVED");
        if (owner == 0) {
            snprintf(rp->text, sizeof rp->text, "FAILED: seat %d is not reserved", id);
        } else if (owner != cid) {
            snprintf(rp->text, sizeof rp->text, "FAILED: seat %d belongs to Client-%d, not you", id, owner);
        } else {
            seats[id] = 0;
            slog(wid, "Resource %d cancelled by Client-%d", id, cid);
            rp->ok = 1;
            snprintf(rp->text, sizeof rp->text, "SUCCESS: seat %d cancelled", id);
        }
        leave_cs(wid);
    }
}

static void send_reply(int wid, int client_id, const reply_t *rp)
{
    char name[64];
    snprintf(name, sizeof name, REPLY_QUEUE_FMT, client_id);
    mqd_t q = mq_open(name, O_WRONLY);
    if (q == (mqd_t)-1) {
        slog(wid, "cannot open reply queue %s: %s", name, strerror(errno));
        return;
    }
    if (mq_send(q, (const char *)rp, sizeof *rp, 0) == -1)
        slog(wid, "mq_send reply failed: %s", strerror(errno));
    mq_close(q);
}

/* ---------- Worker ---------- */
static void *worker(void *arg)
{
    int wid = (int)(intptr_t)arg;
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(wid * 7919) ^ (unsigned)(uintptr_t)&wid;
    request_t rq;

    while (running) {
        struct timespec to;
        clock_gettime(CLOCK_REALTIME, &to);
        to.tv_sec += 1;
        ssize_t n = mq_timedreceive(req_q, (char *)&rq, sizeof rq, NULL, &to);
        if (n < 0) {
            if (errno == ETIMEDOUT || errno == EINTR) continue;
            slog(wid, "mq_receive error: %s", strerror(errno));
            break;
        }
        rq.cmd[sizeof rq.cmd - 1] = '\0';

        if (!strcmp(rq.cmd, "LIST") || !strcmp(rq.cmd, "QUIT") || !strcmp(rq.cmd, "RESET"))
            slog(wid, "received %s from Client-%d", rq.cmd, rq.client_id);
        else
            slog(wid, "received %s %d from Client-%d", rq.cmd, rq.resource_id, rq.client_id);

        reply_t rp;
        handle(wid, &seed, &rq, &rp);
        send_reply(wid, rq.client_id, &rp);
    }
    return NULL;
}

static void on_signal(int sig) { (void)sig; running = 0; }

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "w:s:d:")) != -1) {
        switch (opt) {
        case 'w': num_workers = atoi(optarg); break;
        case 's': use_sync  = strcmp(optarg, "off") != 0; break;
        case 'd': use_delay = strcmp(optarg, "off") != 0; break;
        default:
            fprintf(stderr, "Usage: %s [-w workers] [-s on|off] [-d on|off]\n", argv[0]);
            return 1;
        }
    }
    if (num_workers < 1) num_workers = 1;
    if (num_workers > 32) num_workers = 32;

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;           /* ไม่ใส่ SA_RESTART เพื่อให้ mq_receive ถูกขัดจังหวะได้ */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct mq_attr attr;
    memset(&attr, 0, sizeof attr);
    attr.mq_maxmsg  = QUEUE_DEPTH;
    attr.mq_msgsize = sizeof(request_t);

    mq_unlink(REQ_QUEUE);                /* ล้าง queue เก่าที่อาจค้างอยู่ */
    req_q = mq_open(REQ_QUEUE, O_CREAT | O_RDONLY, 0666, &attr);
    if (req_q == (mqd_t)-1) { perror("mq_open request queue"); return 1; }

    slog(0, "Cinema server started: workers=%d, synchronization=%s, random_delay=%s",
         num_workers, use_sync ? "ON (mutex)" : "OFF", use_delay ? "ON" : "OFF");
    slog(0, "Request queue: %s  (%d seats). Press Ctrl+C to stop.", REQ_QUEUE, NUM_SEATS);

    pthread_t th[32];
    for (int i = 0; i < num_workers; i++)
        pthread_create(&th[i], NULL, worker, (void *)(intptr_t)(i + 1));
    for (int i = 0; i < num_workers; i++)
        pthread_join(th[i], NULL);

    mq_close(req_q);
    mq_unlink(REQ_QUEUE);
    slog(0, "Server stopped.");
    return 0;
}
