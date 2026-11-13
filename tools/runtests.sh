#!/bin/sh
# Runs the tests.
#   tests/NN_*.py        run under the interpreter and under python3; output and exit code must match
#   tests/checked/*.py   the same, and the file must also pass the type checker
#   tests/rejected/*.py  must be refused before running, with the message in the .expected file
# The last two groups need the agda program and are skipped, with a notice, without it.
PAGDA=${1:-./pagda}
pass=0; fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

compare() {   # compare <test file>: output and exit code against python3
    exp=$(python3 "$1" 2>/dev/null </dev/null); ec=$?
    got=$("$PAGDA" "$1" 2>"$tmp/stderr" </dev/null); gc=$?
    if [ "$exp" = "$got" ] && [ $ec -eq $gc ]; then pass=$((pass + 1)); return 0; fi
    fail=$((fail + 1))
    echo "FAIL $1 (exit: python3=$ec pagda=$gc)"
    printf '%s\n' "$exp" > "$tmp/exp"; printf '%s\n' "$got" > "$tmp/got"
    diff "$tmp/exp" "$tmp/got" | head -${DIFF_LINES:-8}
    tail -3 "$tmp/stderr"
    return 1
}

for t in tests/[0-9]*.py; do compare "$t"; done

if command -v "${PAGDA_AGDA:-agda}" >/dev/null 2>&1; then
    # A fresh cache, so that Agda really runs on every file.
    XDG_CACHE_HOME="$tmp/cache"; export XDG_CACHE_HOME
    for t in tests/checked/*.py; do
        [ "$(basename "$t")" = dependent.py ] && continue
        if compare "$t" && ! "$PAGDA" --check "$t" >/dev/null 2>"$tmp/stderr"; then
            pass=$((pass - 1)); fail=$((fail + 1))
            echo "FAIL $t (did not pass the type checker)"; tail -3 "$tmp/stderr"
        fi
    done
    for t in tests/rejected/*.py; do
        case "$t" in */helper_*) continue ;; esac   # files that the tests import
        out=$("$PAGDA" "$t" 2>"$tmp/stderr" </dev/null); code=$?
        if [ $code -eq 1 ] && [ -z "$out" ] && cmp -s "$tmp/stderr" "${t%.py}.expected"; then pass=$((pass + 1))
        else
            fail=$((fail + 1))
            echo "FAIL $t (exit $code; should be refused with the message in ${t%.py}.expected)"
            diff "${t%.py}.expected" "$tmp/stderr" | head -${DIFF_LINES:-8}
        fi
    done
else
    echo "NOTICE: agda not found, so the tests in tests/checked and tests/rejected were skipped"
fi
echo "passed $pass, failed $fail"
[ $fail -eq 0 ]
