#ifndef COMMON_H
#define COMMON_H

/* ค่าคงที่และโครงสร้าง Message ที่ Client/Server ใช้ร่วมกัน */

#define REQ_QUEUE        "/cinema_req"          /* Request Queue (ทุก Client ส่งเข้าที่นี่) */
#define REPLY_QUEUE_FMT  "/cinema_reply_%d"     /* Reply Queue แยกตาม Client ID */
#define NUM_SEATS        20                     /* ที่นั่ง 1-20 */
#define MAX_TEXT         512
#define QUEUE_DEPTH      10                     /* ต้อง <= /proc/sys/fs/mqueue/msg_max (ปกติ 10) */

/* Client -> Server */
typedef struct {
    int  client_id;
    char cmd[16];        /* LIST, STATUS, RESERVE, CANCEL, QUIT, RESET */
    int  resource_id;    /* หมายเลขที่นั่ง (ไม่ใช้กับ LIST/QUIT/RESET) */
} request_t;

/* Server -> Client */
typedef struct {
    int  ok;             /* 1 = SUCCESS, 0 = FAILED */
    char text[MAX_TEXT];
} reply_t;

#endif
