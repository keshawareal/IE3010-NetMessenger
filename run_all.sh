#!/usr/bin/env bash
# run_all.sh - build, run every test suite, print the evidence for the report
# IT23601956 | port 7956 | NID:6019
cd "$(dirname "$0")" || exit 1
REG="IT23601956"; PORT=$((6000 + 1956))

echo "== 1. Build =="
make -f Makefile_1956 clean >/dev/null
make -f Makefile_1956 || { echo "Build failed"; exit 1; }

echo; echo "== 2. Listening check (screenshot this) =="
pkill -x server_1956 2>/dev/null; sleep 0.3
./server_1956 > /dev/null 2>&1 &
SRV=$!; sleep 0.6
if command -v ss >/dev/null 2>&1; then
    ss -tlnp 2>/dev/null | grep ":$PORT" || { echo "Server NOT listening on $PORT"; kill $SRV; exit 1; }
else
    (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null && echo "port $PORT is accepting connections (ss not installed)" \
        || { echo "Server NOT listening on $PORT"; kill $SRV; exit 1; }
fi
kill $SRV; sleep 0.4

echo; echo "== 3. Test suites =="
rm -f "netmsg_$REG.log"; rm -rf storage downloads
FAILED=0
for t in register messaging rooms file errors disconnect; do
    echo; bash "tests/test_$t.sh" || FAILED=$((FAILED+1))
done

echo; echo "== 4. Server log excerpt (netmsg_$REG.log) =="
head -n 25 "netmsg_$REG.log"
echo; echo "== 5. Stored files (./storage/$REG) =="
ls -lR "./storage/$REG"
echo; echo "== Result =="
if [ "$FAILED" -eq 0 ]; then echo "ALL SUITES PASSED"; else echo "$FAILED suite(s) failed"; fi
exit $FAILED
