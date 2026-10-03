# Cinema Seat Reservation System (C + POSIX Message Queue)

ระบบจองที่นั่งโรงภาพยนตร์ 20 ที่นั่ง (หมายเลข 1–20) — Client หลายตัวส่งคำสั่งผ่าน **POSIX Message Queue**
ไปยัง Server ที่มี **Worker Thread หลายตัว** ใช้ **Mutex** ป้องกัน Race Condition

## ไฟล์ในโปรเจกต์
| ไฟล์ | หน้าที่ |
|---|---|
| `server.c` | Server + Worker threads + Shared reservation table + Mutex |
| `client.c` | Client (interactive หรือคำสั่งเดียว) |
| `common.h` | โครงสร้าง Message และชื่อ Queue |
| `race_test.sh` | สคริปต์ให้ Client 5 ตัวจองที่นั่งเดียวกันพร้อมกัน |
| `Makefile`, `Dockerfile` | Build |

## 1. Build Docker Image
เปิด Terminal ในโฟลเดอร์โปรเจกต์ (VS Code: Terminal > New Terminal)
```
docker build -t cinema-mq .
```

## 2. Run Container
```
docker run -it --name cinema cinema-mq
```
(ถ้าสร้างชื่อ `cinema` ไปแล้วและต้องการเริ่มใหม่: `docker rm -f cinema`)

## 3. เปิด Server (Terminal 1 — อยู่ใน container แล้ว)
```
./server                      # 3 workers, sync ON, random delay ON (ค่าเริ่มต้น)
./server -w 1                 # Worker 1 ตัว
./server -w 3 -s off          # ปิด Synchronization (เกิด Race Condition)
./server -w 3 -s on           # เปิด Synchronization (Mutex)
./server -w 3 -s off -d off   # ปิด random delay (ไว้เทียบผล)
```
หยุด Server ด้วย `Ctrl+C`

## 4. เปิด Client หลายตัว (Terminal 2–6)
ในแต่ละ Terminal ใหม่ ให้เข้า container เดิม:
```
docker exec -it cinema bash
./client 1        # Terminal 2 = Client-1
./client 2        # Terminal 3 = Client-2   ... เป็นต้น
```
หรือใน VS Code: ติดตั้ง extension **Docker** → คลิกขวาที่ container `cinema` → *Attach Shell*

## 5. Message Queue ที่ใช้
- **POSIX Message Queue** (`mq_open`, `mq_send`, `mq_timedreceive`) — link ด้วย `-lrt -pthread`
- `/cinema_req` : Request Queue เดียว ทุก Client ส่งเข้า, Worker ทุกตัวแย่งกันอ่าน (competing consumers)
- `/cinema_reply_<client_id>` : Reply Queue ของแต่ละ Client (Worker ตอบกลับตาม Client ID)
- Message: `request_t {client_id, cmd, resource_id}` และ `reply_t {ok, text}` (ดู `common.h`)

## 6. คำสั่งที่ Client รองรับ
```
LIST              แสดงสถานะที่นั่งทั้งหมด
STATUS <n>        ดูสถานะที่นั่ง n
RESERVE <n>       จองที่นั่ง n
CANCEL <n>        ยกเลิก (ยกเลิกได้เฉพาะที่นั่งที่ตัวเองจอง)
QUIT              ออก
RESET             (เสริม) ล้างการจองทั้งหมด ใช้ตอนทดลอง
```
โหมดคำสั่งเดียว: `./client 3 RESERVE 10`

## 7. วิธีทดลอง Race Condition
ใช้สคริปต์ (Client 5 ตัว จอง seat 10 พร้อมกัน):
```
./race_test.sh 10 5
```
หรือทำมือ: เปิด 5 Terminal แล้วพิมพ์ `RESERVE 10` ให้ใกล้เคียงกันที่สุด

| Experiment | Server | ผลที่คาดหวัง |
|---|---|---|
| 1 Sequential baseline | `./server -w 1` | SUCCESS เพียง 1 Client |
| 2 Concurrent, no sync | `./server -w 3 -s off` | มี SUCCESS มากกว่า 1 Client + log `!!! RACE CONDITION DETECTED` |
| 3 Concurrent, sync | `./server -w 3 -s on` | SUCCESS เพียง 1 Client เสมอ |

Random delay 50–500 ms อยู่ระหว่างขั้นตอน check กับ update ใน `RESERVE` (ฟังก์ชัน `random_delay()` ใน `server.c`)
ใช้ขยาย Race Window เพื่อการทดลองเท่านั้น

## 8. วิธีเปิด/ปิด Synchronization
ใช้ option `-s on|off` ตอนเปิด Server (ดูข้อ 3) — Mutex อยู่ที่ฟังก์ชัน `enter_cs()` / `leave_cs()` ใน `server.c`

## 9. ข้อมูลสำหรับรายงาน
- **Shared Data:** `seats[NUM_SEATS+1]` (0 = AVAILABLE, อื่น ๆ = Client ID ผู้จอง)
- **Critical Section:** ช่วงอ่านและเขียน `seats[]` ในทุก handler (`RESERVE` คือ check → delay → update)
- **Mutex:** `table_lock` ครอบ Critical Section ทั้งช่วง (`enter_cs`/`leave_cs`)
- **ทำไม Message Queue อย่างเดียวไม่พอ:** Queue ทำให้ *ข้อความ* ถูกส่งถึง Worker ทีละข้อความ แต่เมื่อ Worker หลายตัวดึงข้อความคนละข้อความ
  แล้วประมวลผลพร้อมกัน ทุกตัวยังเข้าถึง `seats[]` ร่วมกันได้ จึงต้องมี Mutex ปกป้อง check-then-update ให้เป็น atomic
- **Log:** `#seq เวลา [Worker-N] ...` แสดง received / check / entering / leaving critical section
# OS-cinema_reservation
