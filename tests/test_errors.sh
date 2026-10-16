#!/usr/bin/env bash
# Malformed input, partial lines, several lines in one recv, commands before REGISTER
cd "$(dirname "$0")/.." && source tests/lib.sh
echo "== test_errors =="
start_server
run_client out/err_a.txt <<'X'
LIST
REGISTER bad/name
REGISTER tester
FOO bar
JOIN
SENDFILE amal x.bin abc
BCAST
QUIT
X
expect "command before REGISTER"  out/err_a.txt "^ERR 006 NOT_REGISTERED NID:$NID$"
expect "unknown command"          out/err_a.txt "^ERR 007 UNKNOWN_COMMAND NID:$NID$"
expect "invalid username"         out/err_a.txt "^ERR 008 BAD_ARGUMENTS NID:$NID$"
expect "bad SENDFILE size"        out/err_a.txt "^ERR 008 BAD_ARGUMENTS NID:$NID$"

# framing: one command split over several writes + three commands in one write
exec 3<>/dev/tcp/127.0.0.1/$PORT
printf 'REGI' >&3; sleep 0.3; printf 'STER fr' >&3; sleep 0.3; printf 'amer\nLIST\nBCAST hi\n' >&3
sleep 0.5; printf 'QUIT\n' >&3
timeout 2 cat <&3 > out/err_frame.txt; exec 3>&-
expect "partial line reassembled"     out/err_frame.txt "^OK REGISTERED framer NID:$NID$"
expect "multiple lines in one buffer" out/err_frame.txt "^OK USERS framer NID:$NID$"
expect "third line handled"           out/err_frame.txt "^OK SENT NID:$NID$"

# server still alive after all of the above
run_client out/err_b.txt <<'X'
REGISTER still_alive
QUIT
X
expect "server still running"     out/err_b.txt "^OK REGISTERED still_alive NID:$NID$"
stop_server; summary
