---
name: sol-review
description: Get the staged diff reviewed by GPT-6.1 Sol (high) before every Fiddle commit, handle its findings, and stamp the diff so the commit hook lets the commit through. Use before any git commit in this repo.
---

# Sol review before every commit

Opus is Fiddle's main engineer. Every commit gets a review from
**GPT-6.1 Sol at high reasoning effort** before it lands on `main`.
The git hook `.githooks/prepare-commit-msg` refuses an agent's commit
(Claude Code shells have `CLAUDECODE=1`) unless what git is about to commit
matches a passed-review stamp. It checks the index actually being committed,
so `commit -a` or a pathspec commit that picks up unreviewed changes is
refused, and `--no-verify` doesn't skip it. Commits the user types in a
terminal pass. A SessionStart hook in `.claude/settings.json` sets
`git config core.hooksPath .githooks`; on a fresh clone, run that once by hand
if commits aren't being checked.

## Steps

1. **Build and test** as CLAUDE.md says (`pnpm build` before `cmake --build`
   when UI files changed; `bash scripts/test.sh`). Review comes after green tests.
2. **Stage exactly this commit's files** with `git add <paths>`. Never
   `git add -A`: the repo has untracked scratch files. Check
   `git diff --cached --stat`. Later, commit with a plain `git commit` (no
   `-a`, no pathspecs) so the commit is exactly the reviewed index.
3. **Draft the commit message** first; the reviewer gets it as the statement of intent.
4. **Ask Sol for the review** (brief below):
   - **In T3 Code** (`delegate_task` is available): call `delegate_task` with
     `target: {providerInstanceId: "codex", model: "gpt-6.1-sol", options: {reasoningEffort: "high"}}`,
     `role: "review"`, `mode: "wait"`, `timeoutMs: 1800000`, and a title like
     `Sol review: <commit subject>`. Leave `isolation` at its default (shared
     checkout, never a worktree) and omit `runtimeMode`. If the wait times out,
     follow the task's `next` instead of starting a second review.
   - **Outside T3 Code** (plain Claude Code): run Codex directly:
     `codex exec -m gpt-6.1-sol -c model_reasoning_effort="high" -s read-only -C /Users/mhc/fiddle -o <scratchpad>/sol-review.md "<brief>"`
     and read the output file.
5. **Handle the findings.** Fix the valid ones. For any you think are wrong,
   write down why. Minor findings may be left if the reason is recorded.
6. **Re-check if the staged diff changed.** Start a *new* review (a new
   `delegate_task`, not a message to the old thread) with the original brief plus
   the previous findings, what you changed, and your objections. At most two
   re-checks; if Sol and you still disagree on a blocker or major finding, stop
   and ask the user.
7. **Stamp** once the latest review says `VERDICT: PASS`, or every remaining
   finding is minor or rebutted:
   `bash scripts/sol-review.sh stamp "gpt-6.1-sol high: PASS; <n> fixed, <n> rebutted"`.
   Staging anything after this changes the hash and needs a new review.
8. **Commit**, adding `Reviewed-by: gpt-6.1-sol (high)` above the
   `Co-Authored-By` line. Tell the user in one or two lines what Sol found and
   what you fixed or rebutted.

## Review brief

Fill in the angle-bracket parts.

```
Review one commit for Fiddle before it lands on main. Fiddle is a macOS
C++ audio host for Dorico (FiddleServer, CMake, VST3 hosting, realtime audio
engine) with a Svelte UI in a WKWebView. The repo is /Users/mhc/fiddle.

Review ONLY the staged diff: `git diff --cached` (`git diff --cached --stat`
for an overview). Read surrounding code as needed. You are a reviewer, not an
editor: do not modify, stage, unstage, stash or commit anything, and do not run
builds. Build and tests already passed: <what was run>.

Intent (draft commit message):
<commit message>

<For a re-check: previous findings, what changed, and objections.>

Look for: correctness bugs and regressions; realtime-audio and threading safety
(no locks, allocation or blocking I/O on the audio thread); C++ <-> JS IPC
mismatches; persistence, migration and undo/redo; Dorico bridge-map and
expression-map consistency; missing or weak tests; docs that the change makes
wrong. Skip style nits.

For each finding give: severity (blocker / major / minor), file:line, the
problem, a concrete failure scenario, and a suggested fix. End with exactly one
line: `VERDICT: PASS` (no blocker or major findings) or `VERDICT: CHANGES`.
```
