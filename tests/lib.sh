#!/usr/bin/env bash
# Shared helpers for the NetMessenger tests (IT23601956, port 7956, NID 6019)
REG="IT23601956"; PORT=$((6000 + 1956)); NID=6019
PASS=0; FAIL=0
mkdir -p out

start_server() {
    pkill -x server_1956 2>/dev/null; sleep 0.3
    ./server_1956 > out/server.stdout 2>&1 &
    SRV=$!
    disown "$SRV" 2>/dev/null      # so a plain `wait` only waits for the clients
    sleep 0.6
}
stop_server() { kill "$SRV" 2>/dev/null; sleep 0.3; }

# run_client <outfile>  (commands on stdin; a line "sleep N" pauses)
run_client() {
    local out=$1
    { while IFS= read -r l; do
          case "$l" in sleep\ *) sleep "${l#sleep }";; *) printf '%s\n' "$l";; esac
      done; } | ./client_1956 > "$out" 2>&1
}

# expect <description> <file> <egrep-pattern>
expect() {
    if grep -qE -- "$3" "$2"; then PASS=$((PASS+1)); echo "  PASS  $1"
    else FAIL=$((FAIL+1)); echo "  FAIL  $1   (pattern: $3 in $2)"; fi
}
expect_not() {
    if grep -qE -- "$3" "$2"; then FAIL=$((FAIL+1)); echo "  FAIL  $1   (unexpected: $3 in $2)"
    else PASS=$((PASS+1)); echo "  PASS  $1"; fi
}
summary() { echo "  -> $PASS passed, $FAIL failed"; [ "$FAIL" -eq 0 ]; }
