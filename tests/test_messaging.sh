#!/usr/bin/env bash
# BCAST and PMSG
cd "$(dirname "$0")/.." && source tests/lib.sh
echo "== test_messaging =="
start_server
run_client out/msg_amal.txt  <<'X' &
REGISTER amal
sleep 4
QUIT
X
run_client out/msg_kamal.txt <<'X' &
sleep 0.5
REGISTER kamal
sleep 4
QUIT
X
sleep 1.5
run_client out/msg_nimal.txt <<'X'
REGISTER nimal
BCAST hello everyone
PMSG amal secret message here
PMSG ghost hello
PMSG amal
QUIT
X
wait
expect "broadcast delivered to amal"  out/msg_amal.txt  "^MSG BCAST nimal hello everyone$"
expect "broadcast delivered to kamal" out/msg_kamal.txt "^MSG BCAST nimal hello everyone$"
expect "private delivered to amal"    out/msg_amal.txt  "^MSG PRIV nimal secret message here$"
expect_not "private NOT seen by kamal" out/msg_kamal.txt "secret message"
expect "sender gets OK SENT"          out/msg_nimal.txt "^OK SENT NID:$NID$"
expect "PMSG unknown user"            out/msg_nimal.txt "^ERR 002 USER_NOT_FOUND NID:$NID$"
expect "PMSG missing text"            out/msg_nimal.txt "^ERR 008 BAD_ARGUMENTS NID:$NID$"
stop_server; summary
