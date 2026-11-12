#!/usr/bin/env bash
# Demo helpers (design doc §12). Run from anywhere inside the repo.
#
#   demo/demo.sh check                        tools, git identity, remote, base branch
#   demo/demo.sh kickoff                      ./regress on a clean tree, then the green summary
#   demo/demo.sh branch risky    [opts]       new branch: RD# "speed-up" + INC refactor (2 commits)
#   demo/demo.sh branch spec-fix [opts]       new branch: TDVCL 8086-1 checked against the datasheet
#       --base REF    branch point (default: $DEMO_BASE, else origin/main)
#       --name NAME   branch name (default: demo-risky-speedup-HHMM / demo-tdvcl-fix-HHMM)
#       --value NS    spec-fix only: the datasheet's 8086-1 TDVCL min (default: keep the number)
#       --push        push the branch and print the link that opens its PR
#   demo/demo.sh revert inc|speedup [--push]  revert that commit of the risky branch (checked out)
#   demo/demo.sh explain [OUT]                what failed in OUT (default out/): tests, values, shmoo
#   demo/demo.sh stuck-pin                    fault_stuck_ad3 vs regression/expected.yaml
set -euo pipefail

ROOT=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$ROOT"
BIN=build/regress/testprog/i86test
PATCHES=demo/patches/risky-speedup
GRADES=8086,8086-2,8086-1

say() { printf '%s\n' "$*"; }
die() { printf 'demo: %s\n' "$*" >&2; exit 1; }
usage() { sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; }

default_base() {
  if [ -n "${DEMO_BASE:-}" ]; then echo "$DEMO_BASE"; else echo origin/main; fi
}

need_clean_tree() {
  git diff --quiet && git diff --cached --quiet || die "commit or stash your changes first"
}

repo_url() {
  git remote get-url origin | sed -E 's#^git@([^:]+):#https://\1/#; s#https://[^@/]*@#https://#; s#\.git$##'
}

push_and_link() {
  local name=$1 base=$2
  git push -u origin "$name"
  say ""
  say "Open the PR (base ${base#origin/}):"
  say "  $(repo_url)/compare/${base#origin/}...$name?expand=1"
  say "  or: gh pr create --base ${base#origin/} --head $name --fill"
}

cmd_check() {
  local ok=1
  for tool in git cmake ctest nasm; do
    if command -v "$tool" >/dev/null; then say "ok    $tool: $(command -v "$tool")"; else say "MISS  $tool"; ok=0; fi
  done
  if command -v "${CXX:-g++}" >/dev/null; then say "ok    C++ compiler: ${CXX:-g++}"; else say "MISS  C++ compiler (${CXX:-g++}); set CXX"; ok=0; fi
  if command -v gh >/dev/null; then say "ok    gh (optional): $(gh --version | head -n 1)"; else say "info  gh not installed: open PRs from the printed link"; fi
  if [ -n "$(git config user.name)" ] && [ -n "$(git config user.email)" ]; then
    say "ok    git identity: $(git config user.name) <$(git config user.email)>"
  else
    say "MISS  git user.name / user.email (needed to commit)"; ok=0
  fi
  local base; base=$(default_base)
  if git fetch -q origin "${base#origin/}" 2>/dev/null && git rev-parse -q --verify "$base^{commit}" >/dev/null; then
    say "ok    base $base: $(git log -1 --format='%h %s' "$base")"
    if git cat-file -e "$base:$PATCHES" 2>/dev/null; then say "ok    $PATCHES present on the base"; else say "MISS  $PATCHES not on $base (set DEMO_BASE)"; ok=0; fi
  else
    say "MISS  cannot fetch $base (set DEMO_BASE to the demo base branch)"; ok=0
  fi
  [ -x "$BIN" ] && say "ok    $BIN built" || say "info  $BIN not built yet: run demo/demo.sh kickoff"
  [ "$ok" -eq 1 ] && say "== check: ready" || { say "== check: fix the MISS lines"; exit 1; }
}

cmd_kickoff() {
  need_clean_tree
  ./regress 2>&1 | grep -E '^(== |   )' || true
  [ -f out/summary.md ] || die "regress produced no out/summary.md"
  say ""
  head -n 12 out/summary.md
  say ""
  cat out/shmoo_good/shmoo.txt
  say ""
  say "Open: out/summary.md  out/shmoo_good.svg"
  grep -q 'regress: FAIL' out/summary.md && exit 1 || true
}

cmd_branch() {
  local kind=${1:-}; shift || true
  local base name="" value="" push=0
  base=$(default_base)
  while [ $# -gt 0 ]; do
    case "$1" in
      --base) base=$2; shift 2 ;;
      --name) name=$2; shift 2 ;;
      --value) value=$2; shift 2 ;;
      --push) push=1; shift ;;
      *) die "branch: unknown option $1" ;;
    esac
  done
  need_clean_tree
  case "$base" in origin/*) git fetch -q origin "${base#origin/}" ;; esac
  git rev-parse -q --verify "$base^{commit}" >/dev/null || die "no such base $base"

  case "$kind" in
    risky)
      [ -z "$value" ] || die "--value is for spec-fix"
      name=${name:-demo-risky-speedup-$(date +%H%M)}
      local tmp; tmp=$(mktemp -d)
      git archive "$base" "$PATCHES" | tar -x -C "$tmp" || die "$PATCHES not on $base"
      git switch -q -c "$name" "$base"
      for p in "$tmp/$PATCHES"/*.patch; do
        git apply --3way --index "$p" || die "patch $(basename "$p") does not apply on $base"
        git commit -q -m "$(sed -n 's/^Subject: \[PATCH[^]]*\] //p' "$p")"
      done
      rm -rf "$tmp"
      ;;
    spec-fix)
      name=${name:-demo-tdvcl-fix-$(date +%H%M)}
      git switch -q -c "$name" "$base"
      local before; before=$(sed -n '/^  TDVCL:/,/^  [A-Z]/{/"8086-1"/p}' spec/spec.yaml)
      [ -n "$before" ] || die "no TDVCL 8086-1 entry in spec/spec.yaml"
      local num; num=${value:-$(sed -E 's/.*min: *([0-9.]+).*/\1/' <<<"$before")}
      sed -i.bak -E "/^  TDVCL:/,/^  [A-Z]/s/(\"8086-1\": *)\{.*\}/\1{ min: $num }/" spec/spec.yaml
      rm -f spec/spec.yaml.bak
      [ "$(git diff --numstat spec/spec.yaml | cut -f1)" = 1 ] || die "expected a one-line spec change"
      git commit -q -am "spec: TDVCL 8086-1 min ${num} ns, checked against the datasheet (X1)"
      ;;
    *) die "branch: risky or spec-fix" ;;
  esac
  say "On $name (from $base):"
  git log --oneline "$base..HEAD"
  git diff --stat "$base..HEAD"
  [ "$push" -eq 1 ] && push_and_link "$name" "$base"
  return 0
}

cmd_revert() {
  local what=${1:-} push=0 pattern
  [ "${2:-}" = --push ] && push=1
  case "$what" in
    inc) pattern='^Simplify INC' ;;
    speedup) pattern='^Speed up reads' ;;
    *) die "revert: inc or speedup" ;;
  esac
  need_clean_tree
  local sha; sha=$(git log -n 50 --format=%H --grep="$pattern" | head -n 1)
  [ -n "$sha" ] || die "no commit starting with '${pattern#^}' in the last 50 on $(git branch --show-current)"
  git revert --no-edit "$sha"
  if [ "$push" -eq 1 ]; then git push -u origin "$(git branch --show-current)"; fi
}

cmd_explain() {
  local out=${1:-out}
  [ -d "$out" ] || die "no $out/ (run ./regress, or unzip the CI artifact here)"
  grep -h 'regress: FAIL' "$out/summary.md" 2>/dev/null || say "regress: no FAIL line in $out/summary.md"
  if [ -f "$out/unit-tests.log" ] && grep -q 'tests failed' "$out/unit-tests.log"; then
    say ""; say "Unit tests:"; grep -E '^\s+[0-9]+ - ' "$out/unit-tests.log" | sed 's/^\s*/  /'
  fi
  if [ -f "$out/good/results.csv" ]; then
    say ""; say "Good die, failing tests (points failed, first failure):"
    awk -F, 'NR > 1 && $6 != "pass" {
               n[$1]++
               if (!($1 in first)) {
                 msg = $0; for (i = 0; i < 6; i++) msg = substr(msg, index(msg, ",") + 1)
                 first[$1] = $3 " " $4 " V " $5 " C: " msg; order[++k] = $1
               }
             }
             END { if (!k) print "  none"; for (i = 1; i <= k; i++) printf "  %-4s %2d  %s\n", order[i], n[order[i]], first[order[i]] }' \
      "$out/good/results.csv"
  fi
  if [ -f "$out/good/datalog.jsonl" ]; then
    say ""; say "Failing measurements (datalog, worst point per test and param):"
    awk 'function f(k,   r) {
           if (!match($0, "\"" k "\":(\"[^\"]*\"|[^,}]*)")) return ""
           r = substr($0, RSTART + length(k) + 3, RLENGTH - length(k) - 3); gsub(/"/, "", r); return r
         }
         /"result":"fail"/ {
           if (f("param") ~ /^(golden_mismatches|suite_roms_failed|program_pass|halted)$/) next
           key = f("test") " " f("param"); m = f("margin") + 0
           if (!(key in worst)) order[++k] = key
           else if (m >= worst[key]) next
           worst[key] = m
           line[key] = sprintf("  %-4s %-20s %-6s %s V %s C: %s, limit [%s, %s], margin %s  %s", f("test"), f("param"),
                               f("grade"), f("vcc"), f("temp"), f("value") (f("unit") == "" ? "" : " " f("unit")), f("min"), f("max"), f("margin"), f("note"))
         }
         END { if (!k) print "  none"; for (i = 1; i <= k && i <= 20; i++) print line[order[i]] }' "$out/good/datalog.jsonl"
  fi
  for txt in "$out"/shmoo_*/shmoo.txt; do
    [ -f "$txt" ] || continue
    say ""; say "$(dirname "$txt"):"; tail -n 1 "$txt" | sed 's/^/  /'
  done
  local base; base=$(default_base)
  if git rev-parse -q --verify "$base^{commit}" >/dev/null; then
    say ""; say "Commits on $(git branch --show-current) since $base:"
    git log -n 10 --format='  %h %an: %s' --stat=72 "$base..HEAD" -- libs spec testprog
  fi
}

cmd_stuck_pin() {
  [ -x "$BIN" ] || { cmake --preset regress >/dev/null && cmake --build --preset regress >/dev/null; }
  say "Expected (regression/expected.yaml):"
  sed -n '/^  fault_stuck_ad3:/,/\]/p' regression/expected.yaml
  say ""
  "$BIN" --coverage regression/expected.yaml --dies spec/dies --only fault_stuck_ad3 \
         --grade "$GRADES" --vcc 4.5,5.5 --temp 70 --out out/stuck_pin
}

case "${1:-}" in
  check) cmd_check ;;
  kickoff) cmd_kickoff ;;
  branch) shift; cmd_branch "$@" ;;
  revert) shift; cmd_revert "$@" ;;
  explain) shift; cmd_explain "$@" ;;
  stuck-pin) cmd_stuck_pin ;;
  -h|--help|"") usage ;;
  *) usage; exit 2 ;;
esac
