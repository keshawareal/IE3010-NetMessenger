#!/usr/bin/env bash
# REGISTER, LIST, join/leave notifications, NID tag on every response
cd "$(dirname "$0")/.." && source tests/lib.sh
echo "== test_register =="
start_server
run_client out/reg_a.txt <<'X' &
REGISTER amal
sleep 3
LIST
QUIT
X
sleep 1
run_client out/reg_b.txt <<'X'
REGISTER amal
REGISTER nimal
REGISTER nimal
LIST
QUIT
X
wait
expect "register ok"               out/reg_a.txt "^OK REGISTERED amal NID:$NID$"
expect "duplicate name rejected"   out/reg_b.txt "^ERR 001 USERNAME_TAKEN NID:$NID$"
expect "LIST shows both users"     out/reg_b.txt "^OK USERS (amal,nimal|nimal,amal) NID:$NID$"
expect "join notification"         out/reg_a.txt "^MSG JOIN nimal$"
expect "leave notification"        out/reg_a.txt "^MSG LEAVE nimal$"
expect "second REGISTER refused"    out/reg_b.txt "^ERR 009 ALREADY_REGISTERED NID:$NID$"
expect "QUIT answered"             out/reg_a.txt "^OK BYE NID:$NID$"
expect_not "every OK/ERR has NID"  <(cat out/reg_*.txt | grep -E '^(OK|ERR)' | grep -v "NID:$NID") "."
stop_server; summary
