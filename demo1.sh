#!/bin/bash
# Demo 1: Client หลายตัวส่ง "คำสั่งต่างกัน" พร้อมกัน
# ใช้: ./demo1.sh   (ต้องเปิด ./server ไว้ก่อนใน Terminal อื่น)

echo "=== เตรียม: RESET และจองที่นั่ง 5 ไว้ให้ Client-5 ใช้ทดสอบ CANCEL ==="
./client 99 RESET > /dev/null
./client 5 RESERVE 5 > /dev/null

echo
echo "=== ส่งคำสั่งต่างกัน 5 ตัวพร้อมกัน ==="
./client 1 RESERVE 3  | sed 's/^/[C1 RESERVE 3 ] /' &
./client 2 RESERVE 7  | sed 's/^/[C2 RESERVE 7 ] /' &
./client 3 STATUS 12  | sed 's/^/[C3 STATUS 12 ] /' &
./client 4 LIST       | sed 's/^/[C4 LIST       ] /' &
./client 5 CANCEL 5   | sed 's/^/[C5 CANCEL 5  ] /' &
wait

echo
echo "=== ผลสุดท้าย (LIST) ==="
./client 99 LIST
