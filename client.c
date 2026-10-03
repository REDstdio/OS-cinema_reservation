/*
 * client.c - Cinema Seat Reservation Client
 *
 * Usage:
 *   ./client <client_id>                 โหมด interactive (พิมพ์คำสั่งเอง)
 *   ./client <client_id> RESERVE 10      โหมดคำสั่งเดียว (ใช้กับสคริปต์ทดลอง)
 *
 * Commands: LIST | STATUS <n> | RESERVE <n> | CANCEL <n> | QUIT
 *           RESET (คำสั่งเสริมสำหรับการทดลอง)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <mqueue.h>
#include "common.h"

static int client_id;
static mqd_t req_q = (mqd_t)-1, reply_q = (mqd_t)-1;
static char reply_name[64];

static void cleanup(void)
{
    if (req_q   != (mqd_t)-1) mq_close(req_q);
    if (reply_q != (mqd_t)-1) { mq_close(reply_q); mq_unlink(reply_name); }
}

/* return 1 = ทำงานต่อ, 0 = ออก */
static int run_command(const char *line)
{
    request_t rq;
    memset(&rq, 0, sizeof rq);
    rq.client_id = client_id;

    char cmd[32] = "";
    int id = 0;
    int n = sscanf(line, "%31s %d", cmd, &id);
    if (n < 1) return 1;
    for (char *p = cmd; *p; p++) *p = toupper((unsigned char)*p);

    int needs_id = !strcmp(cmd, "STATUS") || !strcmp(cmd, "RESERVE") || !strcmp(cmd, "CANCEL");
    int no_id    = !strcmp(cmd, "LIST") || !strcmp(cmd, "QUIT") || !strcmp(cmd, "RESET");
    if (!needs_id && !no_id) { printf("Unknown command. Use: LIST | STATUS n | RESERVE n | CANCEL n | QUIT\n"); return 1; }
    if (needs_id && n < 2)   { printf("Usage: %s <seat 1-%d>\n", cmd, NUM_SEATS); return 1; }

    snprintf(rq.cmd, sizeof rq.cmd, "%s", cmd);
    rq.resource_id = id;

    if (mq_send(req_q, (const char *)&rq, sizeof rq, 0) == -1) {
        perror("mq_send");
        return 1;
    }

    reply_t rp;
    struct timespec to;
    clock_gettime(CLOCK_REALTIME, &to);
    to.tv_sec += 15;
    if (mq_timedreceive(reply_q, (char *)&rp, sizeof rp, NULL, &to) == -1) {
        printf("Client-%d: no reply from server (%s)\n", client_id, strerror(errno));
        return 1;
    }
    rp.text[MAX_TEXT - 1] = '\0';
    if (strchr(rp.text, '\n')) printf("Client-%d:\n%s", client_id, rp.text);
    else                       printf("Client-%d : %s\n", client_id, rp.text);

    return strcmp(cmd, "QUIT") != 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || (client_id = atoi(argv[1])) <= 0) {
        fprintf(stderr, "Usage: %s <client_id> [COMMAND [seat]]\n", argv[0]);
        return 1;
    }

    snprintf(reply_name, sizeof reply_name, REPLY_QUEUE_FMT, client_id);
    struct mq_attr attr;
    memset(&attr, 0, sizeof attr);
    attr.mq_maxmsg  = QUEUE_DEPTH;
    attr.mq_msgsize = sizeof(reply_t);

    mq_unlink(reply_name);
    reply_q = mq_open(reply_name, O_CREAT | O_RDONLY, 0666, &attr);
    if (reply_q == (mqd_t)-1) { perror("mq_open reply queue"); return 1; }

    req_q = mq_open(REQ_QUEUE, O_WRONLY);
    if (req_q == (mqd_t)-1) {
        fprintf(stderr, "Cannot open %s (%s). Is the server running?\n", REQ_QUEUE, strerror(errno));
        cleanup();
        return 1;
    }

    if (argc > 2) {                       /* one-shot mode */
        char line[128] = "";
        for (int i = 2; i < argc; i++) {
            strncat(line, argv[i], sizeof line - strlen(line) - 2);
            strcat(line, " ");
        }
        run_command(line);
    } else {                              /* interactive mode */
        char line[128];
        printf("Client-%d ready. Commands: LIST | STATUS n | RESERVE n | CANCEL n | QUIT\n", client_id);
        for (;;) {
            printf("client-%d> ", client_id);
            fflush(stdout);
            if (!fgets(line, sizeof line, stdin)) break;
            if (!run_command(line)) break;
        }
    }
    cleanup();
    return 0;
}
