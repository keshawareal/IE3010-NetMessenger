#!/usr/bin/env bash
# JOIN, LEAVE, ROOMS, RMSG
cd "$(dirname "$0")/.." && source tests/lib.sh
echo "== test_rooms =="
start_server
run_client out/room_amal.txt <<'X' &
REGISTER amal
JOIN room1
sleep 4
QUIT
X
run_client out/room_kamal.txt <<'X' &
sleep 0.5
REGISTER kamal
sleep 4
QUIT
X
sleep 1.5
run_client out/room_nimal.txt <<'X'
REGISTER nimal
JOIN room1
ROOMS
RMSG room1 hi room
RMSG noroom x
LEAVE room1
LEAVE room1
RMSG room1 after leaving
QUIT
X
wait
expect "join ok"                     out/room_nimal.txt "^OK JOINED room1 NID:$NID$"
expect "rooms listed"                out/room_nimal.txt "^OK ROOMS room1 NID:$NID$"
expect "room msg delivered"          out/room_amal.txt  "^MSG ROOM room1 nimal hi room$"
expect_not "non-member got no room msg" out/room_kamal.txt "MSG ROOM"
expect "unknown room"                out/room_nimal.txt "^ERR 003 ROOM_NOT_FOUND NID:$NID$"
expect "leave ok"                    out/room_nimal.txt "^OK LEFT room1 NID:$NID$"
expect "leave twice -> error"        out/room_nimal.txt "^ERR 003 ROOM_NOT_FOUND NID:$NID$"
expect "send after leaving refused"  out/room_nimal.txt "^ERR 005 NOT_A_MEMBER NID:$NID$"
stop_server; summary
