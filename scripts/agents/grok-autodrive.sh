#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
# grok-autodrive.sh — make Composer (grok) self-drive a Minni plan to done,
# Codex-style, without a human nudging it between slices.
#
# It kicks Composer off in headless mode, then keeps resuming the SAME session
# until the plan is finished, the agent reports blocked, progress stalls, or a
# round/credit budget is hit. The autonomy contract lives in the repo's
# AGENTS.md ("Autonomous mode"); this script just activates it (token AUTODRIVE)
# and supervises the loop.
#
#   usage:  ./grok-autodrive.sh <plan_id> [extra kickoff text...]
#
#   env:
#     DRY_RUN=1            print the exact grok invocations, run NOTHING, no credits
#     MAX_ROUNDS=20        hard cap on resume rounds (credit budget proxy)
#     STALL_LIMIT=2        consecutive no-progress rounds before giving up
#     MAX_TURNS=40         --max-turns per grok call (agentic turns within a round)
#     EFFORT=high          --effort level (low|medium|high|xhigh|max)
#     MODEL=grok-build     -m model slug (grok-build | grok-composer-2.5-fast)
#     GROK=~/.grok/bin/grok
#     LOG_DIR=./.autodrive  where round transcripts/json land
#
#   "progress" = git HEAD on the working branch advanced during the round.
#   "done"     = agent printed AUTODRIVE_DONE on its final line.
#   "blocked"  = agent printed AUTODRIVE_BLOCKED: ...
#===----------------------------------------------------------------------===#
set -euo pipefail

PLAN_ID="${1:-}"
[ -n "$PLAN_ID" ] || { echo "usage: $0 <plan_id> [extra kickoff text...]" >&2; exit 2; }
shift || true
EXTRA="${*:-}"

DRY_RUN="${DRY_RUN:-0}"
MAX_ROUNDS="${MAX_ROUNDS:-20}"
STALL_LIMIT="${STALL_LIMIT:-2}"
MAX_TURNS="${MAX_TURNS:-40}"
EFFORT="${EFFORT:-high}"
MODEL="${MODEL:-grok-build}"
GROK="${GROK:-$HOME/.grok/bin/grok}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${LOG_DIR:-$SCRIPT_DIR/.autodrive}"
mkdir -p "$LOG_DIR"

# Shared guardrails appended to every call's system prompt (belt + suspenders;
# the durable contract is in AGENTS.md, this re-asserts the non-negotiables).
RULES='AUTODRIVE active. Self-drive the active Minni plan slice by slice without waiting to be told to continue. LOCAL ONLY: never git push, never merge to main. Never mark a slice accepted without a real on-metal serial marker + kernel8.img sha256. When all slices are accepted print exactly AUTODRIVE_DONE on the final line; if genuinely blocked print AUTODRIVE_BLOCKED: <reason>. Do not repeat an identical failing action.'

KICKOFF="AUTODRIVE: You are running autonomously against Minni plan ${PLAN_ID}. \
Read the plan, find the lowest-numbered slice that is not yet accepted, execute it \
fully (implement -> build -> prove on real hardware, cold-cycling the Pi as needed -> \
capture serial markers + kernel8.img sha256 -> mark the slice accepted with that \
evidence -> commit on the working branch), then advance to the next unaccepted slice. \
Keep going until every slice is accepted. Follow AGENTS.md 'Autonomous mode' exactly. \
${EXTRA}"

CONTINUE="AUTODRIVE: Continue. Re-check Minni plan ${PLAN_ID}, take the next unaccepted \
slice, prove it on metal, mark it accepted, commit. If all slices are accepted print \
AUTODRIVE_DONE; if blocked print AUTODRIVE_BLOCKED: <reason>."

git_head() {
  if [ "$DRY_RUN" = "1" ]; then
    # simulate HEAD advancing each round so the progress branch is exercised
    cat "$LOG_DIR/.dry_round" 2>/dev/null || echo 0
    return
  fi
  git -C "$SCRIPT_DIR" rev-parse HEAD 2>/dev/null || echo "no-git"
}

# Build the grok argv for a round. $1 = "kickoff" | "resume:<sid>"
build_cmd() {
  local mode="$1" prompt="$2"
  local -a cmd=( "$GROK" -p "$prompt" -m "$MODEL"
                 --yolo --check --max-turns "$MAX_TURNS" --effort "$EFFORT"
                 --output-format json --rules "$RULES" --no-auto-update )
  if [[ "$mode" == resume:* ]]; then
    cmd+=( --resume "${mode#resume:}" )
  fi
  printf '%s\0' "${cmd[@]}"
}

# Run one round. Echoes the captured sessionId on fd 3 via a temp file.
run_round() {
  local n="$1" mode="$2" prompt="$3"
  local out="$LOG_DIR/round-$(printf '%02d' "$n").json"
  local -a cmd=()
  while IFS= read -r -d '' a; do cmd+=( "$a" ); done < <(build_cmd "$mode" "$prompt")

  if [ "$DRY_RUN" = "1" ]; then
    # narration to stderr so it's visible but NOT captured as the return value
    echo "    [dry-run] would exec:" >&2
    printf '      %q ' "${cmd[@]}" >&2; echo >&2
    # Simulate a JSON envelope so the loop logic itself is exercised end-to-end.
    local fake_sid="dryrun-sid-0001" fake_text="working on a slice"
    local r=0; [ -f "$LOG_DIR/.dry_round" ] && r=$(cat "$LOG_DIR/.dry_round")
    r=$((r+1)); echo "$r" > "$LOG_DIR/.dry_round"
    # On the 2nd simulated round, pretend the plan finished, to prove the exit path.
    if [ "$r" -ge 2 ]; then fake_text="all slices accepted AUTODRIVE_DONE"; fi
    printf '{"text":%s,"stopReason":"EndTurn","sessionId":"%s"}\n' \
      "$(printf '%s' "$fake_text" | python3 -c 'import json,sys;print(json.dumps(sys.stdin.read()))')" \
      "$fake_sid" > "$out"
  else
    set +e
    "${cmd[@]}" > "$out" 2> "$LOG_DIR/round-$(printf '%02d' "$n").stderr"
    local rc=$?
    set -e
    if [ $rc -ne 0 ]; then
      echo "    grok exited rc=$rc (see round-$(printf '%02d' "$n").stderr)" >&2
      [ $rc -eq 130 ] || [ $rc -eq 143 ] && { echo "interrupted"; return 9; }
    fi
  fi
  echo "$out"
}

# Pull a field from the round JSON; tolerate error envelopes.
jget() { python3 -c 'import json,sys
try: d=json.load(open(sys.argv[1]))
except Exception as e: print(""); sys.exit(0)
if d.get("type")=="error": print(""); sys.exit(0)
print(d.get(sys.argv[2],"") or "")' "$1" "$2"; }

echo "=== grok-autodrive ==="
echo "plan_id    : $PLAN_ID"
echo "model      : $MODEL   effort=$EFFORT  max-turns=$MAX_TURNS"
echo "budget     : MAX_ROUNDS=$MAX_ROUNDS  STALL_LIMIT=$STALL_LIMIT"
echo "dry_run    : $DRY_RUN"
echo "log_dir    : $LOG_DIR"
[ "$DRY_RUN" = "1" ] && rm -f "$LOG_DIR/.dry_round"
echo

SID=""
STALL=0
PREV_HEAD="$(git_head)"

for (( round=1; round<=MAX_ROUNDS; round++ )); do
  echo "--- round $round/$MAX_ROUNDS ---"
  if [ -z "$SID" ]; then
    out="$(run_round "$round" "kickoff" "$KICKOFF")" || { echo "round failed"; break; }
  else
    out="$(run_round "$round" "resume:$SID" "$CONTINUE")" || { echo "round failed"; break; }
  fi
  out="$(printf '%s\n' "$out" | tail -n1)"   # last line = path (dry-run prints argv above it)

  text="$(jget "$out" text)"
  newsid="$(jget "$out" sessionId)"
  [ -n "$newsid" ] && SID="$newsid"

  # done / blocked detection
  if printf '%s' "$text" | grep -q 'AUTODRIVE_DONE'; then
    echo "    => AUTODRIVE_DONE — plan complete. stopping."
    echo "done"; exit 0
  fi
  if printf '%s' "$text" | grep -q 'AUTODRIVE_BLOCKED'; then
    echo "    => $(printf '%s' "$text" | grep -o 'AUTODRIVE_BLOCKED:.*' | head -n1)"
    echo "blocked"; exit 3
  fi

  # progress / stall guard (git HEAD advance on the working branch)
  CUR_HEAD="$(git_head)"
  if [ "$CUR_HEAD" != "$PREV_HEAD" ]; then
    echo "    progress: HEAD ${PREV_HEAD:0:8} -> ${CUR_HEAD:0:8}"
    STALL=0; PREV_HEAD="$CUR_HEAD"
  else
    STALL=$((STALL+1))
    echo "    no git progress this round (stall $STALL/$STALL_LIMIT)"
    if [ "$STALL" -ge "$STALL_LIMIT" ]; then
      echo "    => stalled $STALL rounds with no progress and no done/blocked signal. stopping to save credits."
      echo "stalled"; exit 4
    fi
  fi
  echo "    session: $SID"
done

echo "=> hit MAX_ROUNDS ($MAX_ROUNDS) budget cap without completing. stopping."
echo "budget-exhausted"; exit 5
