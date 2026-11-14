#!/usr/bin/env bash
# Sol review stamp for the staged diff. See .claude/skills/sol-review/SKILL.md.
#
#   bash scripts/sol-review.sh stamp "<note>"   record that the staged diff passed review
#   bash scripts/sol-review.sh check            exit 0 if the staged diff is stamped (or empty)
#   bash scripts/sol-review.sh hash             print the staged diff's hash
#
# The stamp lives in .git/, so it is never committed. Any change to the staged
# diff (a fix, an extra file) changes the hash and needs a new review.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"
stamp_file="$(git rev-parse --git-dir)/sol-review-stamp"

staged_hash() {
  git diff --cached --binary --full-index | shasum -a 256 | cut -d' ' -f1
}

case "${1:-}" in
  hash)
    staged_hash
    ;;
  stamp)
    if git diff --cached --quiet; then
      echo "sol-review: nothing is staged; stage the commit's files first" >&2
      exit 1
    fi
    printf '%s\t%s\t%s\n' "$(staged_hash)" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "${2:-}" > "$stamp_file"
    echo "sol-review: stamped $(git diff --cached --name-only | wc -l | tr -d ' ') staged file(s)"
    ;;
  check)
    git diff --cached --quiet && exit 0
    [[ -f "$stamp_file" ]] || exit 1
    [[ "$(cut -f1 "$stamp_file")" == "$(staged_hash)" ]]
    ;;
  *)
    echo "usage: bash scripts/sol-review.sh stamp \"<note>\" | check | hash" >&2
    exit 2
    ;;
esac
