#!/bin/bash
# ให้ Client 5 ตัวส่ง RESERVE ที่นั่งเดียวกันพร้อมกัน  (ใช้: ./race_test.sh [seat] [num_clients])
SEAT=${1:-10}
N=${2:-5}
echo "=== $N clients RESERVE seat $SEAT at the same time ==="
./client 99 RESET > /dev/null
for i in $(seq 1 $N); do
    ./client $i RESERVE $SEAT &
done
wait
echo "=== Final state ==="
./client 99 STATUS $SEAT
