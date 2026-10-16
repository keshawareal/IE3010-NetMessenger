#!/usr/bin/env bash
# Ungraceful disconnect (client killed with SIGKILL) must be cleaned up
cd "$(dirname "$0")/.." && source tests/lib.sh
echo "== test_disconnect =="
start_server
rm -f out/ghost.fifo; mkfifo out/ghost.fifo
./client_1956 < out/ghost.fifo > out/dc_ghost.txt 2>&1 &
CP=$!
exec 4> out/ghost.fifo
echo "REGISTER ghost" >&4
echo "JOIN room9" >&4
sleep 1
kill -9 $CP; exec 4>&-; sleep 1
run_client out/dc_b.txt <<'X'
REGISTER watcher
LIST
ROOMS
QUIT
X
expect "ghost registered first"      out/dc_ghost.txt "^OK REGISTERED ghost NID:$NID$"
expect_not "ghost removed from user list" out/dc_b.txt "^OK USERS .*ghost"
expect_not "ghost's room cleaned up"      out/dc_b.txt "^OK ROOMS room9"
expect "disconnect logged"           "$(ls netmsg_$REG.log)" "DISCONNECT user=ghost"
expect "server survived"             out/dc_b.txt "^OK BYE NID:$NID$"
rm -f out/ghost.fifo
stop_server; summary
