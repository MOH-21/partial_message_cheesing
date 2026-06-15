#!/usr/bin/env bash
# compare.sh — build both implementations, run them against the same exchange
# server, and grade the student's work.
#
# Usage:
#   bash tests/compare.sh                       # defaults
#   bash tests/compare.sh --trials 3000 --decision-us 500
#
# Run from the assignment/ directory:
#   cd assignment && bash tests/compare.sh

set -uo pipefail

TRIALS=3000
DECISION_US=300
PORT=9001
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# ── Parse args ────────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case $1 in
        --trials)      TRIALS=$2;      shift 2 ;;
        --decision-us) DECISION_US=$2; shift 2 ;;
        *) echo "Unknown argument: $1"; exit 1 ;;
    esac
done

# ── Build ─────────────────────────────────────────────────────────────────────
echo "Building exchange server..."
(cd "$ROOT/common"   && make -s)

echo "Building reference solution..."
(cd "$ROOT/solution" && make -s)

echo "Building student template..."
if ! (cd "$ROOT/template" && make -s 2>&1); then
    echo ""
    echo "[FAIL] Student code did not compile. Fix the build errors above."
    exit 1
fi

echo ""

# ── Start exchange server ─────────────────────────────────────────────────────
setsid "$ROOT/common/bin/exchange_server" >/dev/null 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null' EXIT
sleep 0.3   # let the server bind and listen

# ── Run implementations ───────────────────────────────────────────────────────
run_client() {
    local binary=$1
    "$binary" --trials "$TRIALS" --decision-us "$DECISION_US"
}

echo "── Student implementation ────────────────────────────────────────────"
STUDENT_OUT=""
if ! STUDENT_OUT=$(run_client "$ROOT/template/bin/cheese_client" 2>&1); then
    echo "[FAIL] Student client exited with a non-zero status."
    echo "$STUDENT_OUT"
    PASS=false
else
    echo "$STUDENT_OUT"
fi

echo ""
echo "── Reference solution ────────────────────────────────────────────────"
SOLUTION_OUT=$(run_client "$ROOT/solution/bin/cheese_client")
echo "$SOLUTION_OUT"

# ── Extract metrics ───────────────────────────────────────────────────────────
extract() {
    # extract_metric <output> <mode_label> <stat>
    # e.g. extract "$OUT" "naive" "p50"
    local out="$1" mode="$2" stat="$3"
    echo "$out" | grep "^ *$mode" | grep -oP "${stat}=\s*\K[0-9]+" | head -1 || true
}

S_NAIVE_P50=$(extract  "$STUDENT_OUT"  "naive"  "p50")
S_CHEESE_P50=$(extract "$STUDENT_OUT"  "cheese" "p50")
R_NAIVE_P50=$(extract  "$SOLUTION_OUT" "naive"  "p50")
R_CHEESE_P50=$(extract "$SOLUTION_OUT" "cheese" "p50")

# ── Print comparison table ────────────────────────────────────────────────────
echo ""
echo "── Comparison (p50 latency, ns) ─────────────────────────────────────"
printf "  %-10s  %14s  %14s\n" "mode" "student" "reference"
printf "  %-10s  %14s  %14s\n" "naive"  "${S_NAIVE_P50} ns"  "${R_NAIVE_P50} ns"
printf "  %-10s  %14s  %14s\n" "cheese" "${S_CHEESE_P50} ns" "${R_CHEESE_P50} ns"
echo ""

# ── Grading ───────────────────────────────────────────────────────────────────
PASS=true

check_pass() { echo "  [PASS] $1"; }
check_fail() { echo "  [FAIL] $1"; PASS=false; }
check_info() { echo "  [INFO] $1"; }

# 1. Naive mode must produce a plausible latency (> 0, < 10 ms)
if [[ -n "$S_NAIVE_P50" && "$S_NAIVE_P50" -gt 0 && "$S_NAIVE_P50" -lt 10000000 ]]; then
    check_pass "naive mode runs and produces valid latency (${S_NAIVE_P50} ns)"
else
    check_fail "naive mode latency is missing or implausible (got: '${S_NAIVE_P50}')"
fi

# 2. Cheese mode must produce a plausible latency
if [[ -n "$S_CHEESE_P50" && "$S_CHEESE_P50" -gt 0 && "$S_CHEESE_P50" -lt 10000000 ]]; then
    check_pass "cheese mode runs and produces valid latency (${S_CHEESE_P50} ns)"
else
    check_fail "cheese mode latency is missing or implausible (got: '${S_CHEESE_P50}')"
fi

# 3. Student cheese must be within 3× the reference cheese
#    (loose bound — loopback noise is real; we care about structure, not numbers)
if [[ -n "$S_CHEESE_P50" && -n "$R_CHEESE_P50" && "$R_CHEESE_P50" -gt 0 ]]; then
    THRESHOLD=$(( R_CHEESE_P50 * 3 ))
    if [[ "$S_CHEESE_P50" -le "$THRESHOLD" ]]; then
        check_pass "cheese p50 within 3× of reference (${S_CHEESE_P50} ≤ ${THRESHOLD} ns)"
    else
        check_fail "cheese p50 too high — is TCP_NODELAY set and pre-staging correct? (${S_CHEESE_P50} vs threshold ${THRESHOLD} ns)"
    fi
fi

# 4. Structural check: student cheese p50 should be lower than student naive p50
#    over enough trials. On loopback this isn't guaranteed due to noise, so we
#    only warn rather than fail.
if [[ -n "$S_NAIVE_P50" && -n "$S_CHEESE_P50" ]]; then
    if [[ "$S_CHEESE_P50" -lt "$S_NAIVE_P50" ]]; then
        check_pass "cheese p50 < naive p50 — pre-staging is reducing critical-path latency"
    else
        check_info "cheese p50 >= naive p50 on this run — loopback noise can flip this for small messages."
        check_info "Try --decision-us 1000 or adding network delay:  sudo tc qdisc add dev lo root netem rate 1mbit"
    fi
fi

echo ""

# Bonus tiers
if [[ -n "$S_CHEESE_P50" && -n "$R_CHEESE_P50" ]]; then
    if [[ "$S_CHEESE_P50" -le "$R_CHEESE_P50" ]]; then
        echo "  ★ BONUS: your cheese p50 (${S_CHEESE_P50} ns) matches or beats the reference (${R_CHEESE_P50} ns)!"
        echo "           If you used a different technique (MSG_MORE timing, TCP_CORK, etc.), note it in a comment."
    fi
fi

echo ""
if $PASS; then
    echo "  RESULT: PASS"
else
    echo "  RESULT: FAIL — see [FAIL] lines above"
fi
