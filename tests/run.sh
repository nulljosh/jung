#!/bin/bash
# Jung test runner -- compares stdout of .jung files against .expected files

set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
JUNG="$DIR/../jung"

if [ ! -x "$JUNG" ]; then
    echo "Error: jung binary not found. Run 'make' first."
    exit 1
fi

pass=0
fail=0
errors=""

for test_file in "$DIR"/*.jung; do
    name="$(basename "$test_file" .jung)"
    expected="$DIR/$name.expected"

    if [ ! -f "$expected" ]; then
        echo -e "\033[33mSKIP\033[0m $name (no .expected file)"
        continue
    fi

    actual=$("$JUNG" "$test_file" 2>&1) || true

    if [ "$actual" = "$(cat "$expected")" ]; then
        echo -e "\033[32mPASS\033[0m $name"
        pass=$((pass + 1))
    else
        echo -e "\033[31mFAIL\033[0m $name"
        diff --color=always <(echo "$actual") "$expected" || true
        fail=$((fail + 1))
        errors="$errors $name"
    fi
done

echo ""
echo "Results: $pass passed, $fail failed"

if [ $fail -gt 0 ]; then
    echo "Failed:$errors"
    exit 1
fi
