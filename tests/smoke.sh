#!/bin/sh
set -eu

BIN="./bin/minish"

output="$(
  printf 'echo hello\nprintf "$$"\necho sample > /tmp/minish-smoke.txt\ncat < /tmp/minish-smoke.txt\nfalse\nstatus\nexit\n' |
  "$BIN"
)"

printf '%s\n' "$output" | grep -q "hello"
printf '%s\n' "$output" | grep -Eq '[0-9]+'
printf '%s\n' "$output" | grep -q "sample"
printf '%s\n' "$output" | grep -q "exit value 1"

rm -f /tmp/minish-smoke.txt

echo "smoke test passed"
