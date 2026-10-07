# Cinema Seat Reservation System (C + POSIX Message Queue)

ระบบจองที่นั่งโรงภาพยนตร์ 20 ที่นั่ง (หมายเลข 1–20) พัฒนาด้วยภาษา C บน Linux/Docker โดยใช้ **POSIX Message Queue** สำหรับการสื่อสารระหว่าง Client และ Server พร้อม Worker Threads หลายตัว และใช้ **Mutex** เพื่อป้องกัน Race Condition

## สารบัญไฟล์ในโปรเจกต์

| ไฟล์ | หน้าที่ |
|---|---|
| `server.c` | โค้ดฝั่ง Server, Worker Threads, Shared Reservation Table และ Mutex |
| `client.c` | โค้ดฝั่ง Client (รองรับทั้งแบบ Interactive และแบบคำสั่งเดียว) |
| `common.h` | โครงสร้างข้อมูล Message และชื่อ Queue ที่ใช้งานร่วมกัน |
| `demo1.sh` | สคริปต์อัตโนมัติสำหรับจำลองการส่งคำสั่งหลากหลายรูปแบบ (Demo 1) |
| `race_test.sh` | สคริปต์จำลองให้ Client 5 ตัวส่งคำขอจองที่นั่งเดียวกันพร้อมกัน |
| `Makefile`, `Dockerfile` | สำหรับการ Build และรันบน Docker Container |

---

## 1. วิธี Build Docker Image
เปิด Terminal ในโฟลเดอร์โปรเจกต์ แล้วรันคำสั่ง:
```bash
docker build -t cinema-mq .
```

## 2. วิธี Run Container
```bash
docker run -it --name cinema cinema-mq
```
*(หากต้องการลบ Container เดิมและเริ่มใหม่: `docker rm -f cinema`)*

## 3. วิธีเปิด Server (Terminal 1)
เปิด Server ภายใน Container โดยเลือกอ็อพชันตามต้องการ:
```bash
./server                  # ค่าเริ่มต้น: 3 workers, sync (on), random delay (on)
./server -w 1             # ใช้ Worker 1 ตัว (สำหรับ Sequential Baseline)
./server -w 3 -s off      # เปิด 3 workers, ปิด Synchronization (เพื่อทดสอบ Race Condition)
./server -w 3 -s on       # เปิด 3 workers, เปิด Synchronization (Mutex)
./server -w 3 -s off -d off # ปิด random delay ไว้เทียบผล
```
*กด `Ctrl+C` เพื่อหยุดการทำงานของ Server*

## 4. วิธีเปิด Client หลายตัว (Terminal 2–6)
เปิด Terminal ใหม่แล้วเข้าสู่ Container เดิม:
```bash
docker exec -it cinema bash
./client 1        # Terminal 2 = Client-1
./client 2        # Terminal 3 = Client-2  (...เป็นต้น)
```
หรือใช้งานในโหมดคำสั่งเดียว เช่น:
```bash
./client 3 RESERVE 10
```

---

## 5. รูปแบบ Message Queue ที่ใช้
- **POSIX Message Queue** (`mq_open`, `mq_send`, `mq_timedreceive`) — Link ร่วมกับ `-lrt -pthread`
- `/cinema_req` : Request Queue กลางสำหรับรับคำสั่งจาก Client ทุกตัว (Worker ทุกตัวจะแย่งกันอ่านแบบ *Competing Consumers*)
- `/cinema_reply_<client_id>` : Reply Queue เฉพาะของแต่ละ Client เพื่อรับผลลัพธ์กลับจาก Server
- **Message Structure (`common.h`):**
  - `request_t`: ประกอบด้วย `client_id`, `cmd`, `resource_id`
  - `reply_t`: ประกอบด้วย `ok`, `text`

---

## 6. คำสั่งที่ Client รองรับ
- `LIST` : แสดงสถานะที่นั่งทั้งหมด (1–20)
- `STATUS <n>` : ดูสถานะของที่นั่งหมายเลข `n`
- `RESERVE <n>` : จองที่นั่งหมายเลข `n`
- `CANCEL <n>` : ยกเลิกการจองที่นั่งหมายเลข `n` (ยกเลิกได้เฉพาะที่นั่งที่ตนเองเป็นเจ้าของเท่านั้น)
- `QUIT` : ออกจากโปรแกรม Client
- `RESET` : (คำสั่งเสริม) ล้างสถานะการจองทั้งหมดเพื่อเริ่มต้นทดลองใหม่

---

## 7. ขั้นตอนการสาธิตระบบ (Demo Scenarios)

### Demo 1: Client ส่งคำสั่งทั่วไปพร้อมกัน (Normal Operations)
* **จุดประสงค์:** แสดงการทำงานพื้นฐานของระบบที่รองรับหลาย Client และหลาย Worker ผ่าน Message Queue โดยใช้สคริปต์อัตโนมัติช่วยจำลองการส่งคำสั่งที่หลากหลาย
* **วิธีทดลอง:**
  1. เปิด Server ปกติใน Terminal 1: 
     ```bash
     ./server -w 3 -s on
     ```
  2. เปิด Terminal ใหม่ (หรือเข้า Container เพิ่มเติม) แล้วรันสคริปต์ `demo1.sh`:
     ```bash
     ./demo1.sh
     ```
  3. **ทางเลือกแบบทำมือ:** เปิด Client หลายตัวพร้อมกันในแต่ละ Terminal แล้วทดลองพิมพ์คำสั่งสลับกัน เช่น `LIST`, `RESERVE 5`, `STATUS 5`, `CANCEL 5`
  4. **สังเกตผลลัพธ์:** ดู Log ของ Server ว่า Worker แต่ละตัวช่วยกันหยิบคำขอไปประมวลผลและตอบกลับถูกต้องตามลำดับ

### Demo 2: ทดสอบ Race Condition โดยไม่มี Synchronization
* **จุดประสงค์:** จำลองปัญหา Race Condition เมื่อมี Client หลายตัวพยายามจองทรัพยากรเดียวกันพร้อมกันโดยไม่มีการล็อกข้อมูล (Critical Section)
* **วิธีทดลอง:**
  1. เปิด Server โดยปิดการซิงโครไนซ์และใช้ Worker หลายตัว:
     ```bash
     ./server -w 3 -s off
     ```
  2. ใช้สคริปต์ทดสอบให้ Client 5 ตัวจองที่นั่งเดียวกัน (เช่น ที่นั่ง 10) พร้อมกัน:
     ```bash
     ./race_test.sh 10 5
     ```
  3. **ผลลัพธ์ที่คาดหวัง:** จะพบว่ามี Client มากกว่า 1 ตัวได้รับสถานะ **SUCCESS** พร้อมกัน และ Server จะแสดงข้อความเตือน `!!! RACE CONDITION DETECTED` เนื่องจากไม่มี Mutex ป้องกันช่วง Check-then-Update

### Demo 3: การแก้ไข Race Condition ด้วย Mutex Synchronization
* **จุดประสงค์:** พิสูจน์ว่าเมื่อเปิดใช้งาน Mutex แล้ว ปัญหา Race Condition จะหมดไป และมี Client สำเร็จเพียงรายเดียวเท่านั้น
* **วิธีทดลอง:**
  1. รีเซ็ตสถานะและเปิด Server แบบเปิดใช้ Mutex:
     ```bash
     ./server -w 3 -s on
     ```
  2. รันสคริปต์ทดสอบจองที่นั่งเดียวกันพร้อมกันอีกครั้ง:
     ```bash
     ./race_test.sh 10 5
     ```
  3. **ผลลัพธ์ที่คาดหวัง:** แม้จะมี Random Delay (`50–500 ms` เพื่อขยายช่องว่าง Race Window) แต่ระบบจะให้ผลลัพธ์ที่ถูกต้องเสมอ โดยมี **Client เพียง 1 ตัวเท่านั้นที่จองสำเร็จ** และที่เหลือจะได้รับสถานะ **FAILED** ทันที ดังตัวอย่าง:
     * Client 1: FAILED
     * Client 2: FAILED
     * **Client 3: SUCCESS**
     * Client 4: FAILED
     * Client 5: FAILED

---

## 8. รายละเอียดเชิงเทคนิค (สำหรับรายงาน)
- **Shared Data:** อาเรย์ `seats[NUM_SEATS + 1]` โดยค่า `0` หมายถึงว่าง (`AVAILABLE`) และค่าอื่น ๆ หมายถึง `client_id` ของผู้จอง
- **Critical Section:** ช่วงเวลาที่ Worker ทำการตรวจสอบ (Check) และปรับปรุง (Update) สถานะที่นั่งในอาเรย์ `seats[]`
- **Mutex Mechanism:** ใช้ `table_lock` (pthread mutex) ครอบบริเวณ Critical Section ผ่านฟังก์ชัน `enter_cs()` และ `leave_cs()` ใน `server.c`
- **ทำไม Message Queue อย่างเดียวจึงไม่เพียงพอ:** เพราะ Message Queue ทำหน้าที่เพียงส่งข้อความคำขอมาเข้าคิวให้ Server รับทีละข้อความ แต่เมื่อ Worker หลายตัวดึงข้อความออกไปประมวลผลพร้อมกันในเวลาไล่เลี่ยกัน Worker เหล่านั้นยังคงเข้าถึงและแก้ไขตัวแปร `seats[]` ตัวเดียวกันได้โดยตรง จึงจำเป็นต้องใช้ Mutex เข้ามาควบคุมความเป็น Atomic ของข้อมูลร่วมด้วย