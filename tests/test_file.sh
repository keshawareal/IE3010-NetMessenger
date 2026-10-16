#!/usr/bin/env bash
# SENDFILE to a user and to a room, byte-exact, stored under the personalised path
cd "$(dirname "$0")/.." && source tests/lib.sh
echo "== test_file =="
rm -rf downloads "storage/$REG"
head -c 300000 /dev/urandom > out/testfile.bin
head -c 11000000 /dev/zero  > out/big.bin
start_server
run_client out/file_amal.txt <<'X' &
REGISTER amal
JOIN files
sleep 8
QUIT
X
sleep 1
run_client out/file_kamal.txt <<'X'
REGISTER kamal
JOIN files
SENDFILE amal out/testfile.bin
SENDFILE files out/testfile.bin
SENDFILE ghost out/testfile.bin
SENDFILE amal out/big.bin
LIST
sleep 2
QUIT
X
wait
expect "file accepted"            out/file_kamal.txt "^OK FILE_RECEIVED testfile.bin NID:$NID$"
expect "unknown target"           out/file_kamal.txt "^ERR 002 USER_NOT_FOUND NID:$NID$"
expect "too large rejected"       out/file_kamal.txt "^ERR 004 FILE_TOO_LARGE NID:$NID$"
expect "framing intact after errors (LIST works)" out/file_kamal.txt "^OK USERS .* NID:$NID$"
expect "receiver saw the file"    out/file_amal.txt  "^\[file received\] from=kamal name=testfile.bin size=300000"
if cmp -s out/testfile.bin "storage/$REG/kamal/testfile.bin"; then PASS=$((PASS+1)); echo "  PASS  server copy identical (./storage/$REG/kamal/testfile.bin)"
else FAIL=$((FAIL+1)); echo "  FAIL  server copy differs"; fi
if cmp -s out/testfile.bin downloads/kamal/testfile.bin; then PASS=$((PASS+1)); echo "  PASS  receiver copy identical"
else FAIL=$((FAIL+1)); echo "  FAIL  receiver copy differs"; fi
rm -f out/big.bin
stop_server; summary
