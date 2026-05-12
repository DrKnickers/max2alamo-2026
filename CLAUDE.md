# Working with Claude in this repo

How Claude collaborates here. Read top-to-bottom; the working principles
inform everything else.

This repo is end-to-end authored by Claude under [@DrKnickers](https://github.com/DrKnickers)'s
direction (see [Authorship in the README](README.md#authorship)). That makes
this file load-bearing — it's the operating manual the next session will
read first, alongside [`docs/development-log.md`](docs/development-log.md).

---

## Mindset

Work as a **peer and technical collaborator**, not a one-shot executor. The
goal is a continuous back-and-forth dialogue aimed at producing the highest
quality result that most accurately reflects the user's real intent. A
shorter answer that opens dialogue beats a longer one that closes it.

---

## Working principles

1. **Think before coding** — state assumptions, surface tradeoffs, ask
   before implementing.
2. **Simplicity first** — minimum code that solves the problem. No
   speculative generality.
3. **Surgical changes** — touch only what the request requires. Don't
   clean up adjacent code.
4. **Goal-driven execution** — for multi-step tasks, state a brief plan
   with a verify step for each item before starting.
5. **No laziness** — find root causes. No temporary patches that paper
   over the real bug.
6. **Minimal impact** — only touch what's necessary.

---

## Process

### Plan mode (default for non-trivial work)

- Enter plan mode for **any** task with 3+ steps (use Claude Code's
  ExitPlanMode tool when ready).
- Use the **TodoWrite tool** to track progress across the session.
- For phase-scale work, the corresponding [GitHub issue](https://github.com/DrKnickers/max2alamo-2026/issues)
  is the durable plan; the in-session todos are the working surface.
- Mark items complete as you go. Add a high-level summary at each step.
- **If something goes sideways: STOP and re-plan immediately.** Don't
  push through with the current plan when the assumptions it rested on
  have shifted.

#### Plan structure (for phases and other non-trivial work)

A serious plan covers all five of:

1. **Goal + scope.** One paragraph on what ships when this is done.
   An explicit *In* / *Out* list — what's included, what's deferred,
   and *why*. Out-of-scope items name their reason
   ("separate phase", "out-of-scope for v1", "future PR if anyone asks")
   so a future reader can tell whether the omission was deliberate or
   just forgotten.
2. **What we already have.** A short survey of the existing code and
   conventions the plan leans on. Concrete `file:line` references
   where useful. Forces exploration before design and prevents
   reinventing what's already there.
3. **Architecture / implementation approach.** New APIs (with
   signatures and brief docstrings), data flow, key design decisions.
   Address the questions a reviewer would ask: why this approach over
   the obvious alternatives, what's the structural shape, where do
   new files / functions live.
4. **Risks named up front + mitigations.** A numbered list. For each
   risk, one paragraph naming the hazard concretely (what breaks,
   when, why) and the specific mitigation. Risks that would only
   matter under unrealistic conditions get explicitly accepted ("not
   worth designing around"). The mitigation isn't "be careful" —
   it's a code-level intervention or a documented process step.
5. **Verification.** A checklist organized by category (unit tests,
   corpus round-trip, in-Max validation, etc.). Each line is a
   verifiable claim, not a vague intent. Items that need a separate
   file or scenario name them.

After writing the plan, **summarize for the user before starting work** —
the summary leads with the architectural decisions, calls out the biggest
risks and mitigations, and asks any remaining clarifying questions. Don't
proceed until the user confirms or adjusts the scope. The cost of
misaligned scope is higher than the cost of a 2-minute check-in.

For larger phases (Phase 4 geometry, Phase 5 skinning, Phase 8 animation)
iterate the risks list with the user *before* writing code — a planning
conversation that surfaces a sharp risk pre-coding is dramatically
cheaper than rediscovering it during testing.

### Verification before done

- Never mark a task complete without proving it works.
- The bar: *"Would a staff engineer approve this?"* Run tests, check
  logs, demonstrate correctness.
- Quote the proof in the summary, not just "done".
- For format work specifically: corpus round-trip pass rate is the
  oracle. Anything below 100% on the existing corpus is a regression
  to investigate, not a number to wave at.

### Self-improvement loop

- After **any** correction the user makes, internalize the lesson.
- If the lesson is general enough to apply across future sessions,
  fold it into this `CLAUDE.md` (in the relevant section, not as a
  separate "lessons" appendix).
- Iterate ruthlessly until the mistake rate drops to zero on that
  class of problem.

### Demand elegance (balanced)

- Pause on non-trivial code: *"is there a more elegant way?"*
- **Skip this for simple fixes** — don't over-engineer.
- The format library especially deserves elegance — it's the
  correctness oracle for the Max plugin and lives a long time.

### Autonomous bug fixing

- When given a bug report, just fix it. Zero context-switching
  required from the user beyond the original report.
- For format-spec bugs: confirm against the corpus first
  (`alo_dump`, `alo_roundtrip`) before patching the C++ — the
  corpus is the ground truth, the docs are the working
  understanding.

---

## Development log: when to update [`docs/development-log.md`](docs/development-log.md)

The development log is the single-file project status doc that future
sessions open first. Keeping it accurate is a Claude responsibility, not
the user's.

### Mandatory updates

**After every phase ships**, in the same PR that closes the phase issue:

1. Update the **phase status table** row (status → ✅ shipped, fill in
   "acceptance proof" with the concrete result, e.g. *"4929 / 4929
   round-trips"*, *"Plugin loads in Max"*).
2. Add a **per-phase detail section** under the existing per-phase
   detail block. Cover: what shipped (linked commits), key decisions
   made, format-spec corrections discovered (link out to
   `docs/format-notes.md` for the full story), and anything a future
   phase will need to know.
3. Strike the phase from **Future phase plans** (or rewrite that
   section to reflect the now-known scope of the next phase).
4. Sweep the **Open format questions** section: anything resolved
   gets marked resolved with the resolution in one sentence; anything
   newly surfaced gets added.

### Recommended updates between phases

Update mid-phase or off-phase when the work materially changes the
project state. Concrete triggers:

- **Workflow / convention change** — e.g. when we switched to
  PR-per-change, or when the public flip happened mid-Phase 2.
- **Tooling change** — new dependency in CMake, new CI step, new
  external tool (Ghidra install, JDK requirement, etc.).
- **Repo administrative change** — visibility flip, branch
  protection rules, interaction limits, settings touched via
  `gh api`.
- **Risk identified that affects a future phase** — surface it in
  that phase's *Future phase plans* entry so it's seen before the
  phase starts, not during.
- **Self-review pass** that surfaces fixes worth recording (e.g. the
  Phase 1 self-review caught the printf format bug + 0x402 fixed-size
  finding).

### Where things go (don't double-write)

| Information | Belongs in |
|---|---|
| Format spec (chunk IDs, layouts, encodings, citations) | [`docs/format-notes.md`](docs/format-notes.md) |
| Build commands and SDK paths | [`docs/build.md`](docs/build.md) |
| Test corpus extraction | [`docs/corpus.md`](docs/corpus.md) |
| Per-PR detail (what files changed, line counts, commit messages) | The PR description and commit message itself |
| Project-state summary, phase tracking, where things live, conventions | [`docs/development-log.md`](docs/development-log.md) |
| Working agreement, principles, when-to-update conventions | This file (`CLAUDE.md`) |

When in doubt, link rather than duplicate. The development log is a
*navigator*, not a complete record — it points at the canonical sources.

### When *not* to update the log

- Trivial fixes (typo, single-line refactor) unless part of a larger
  documented flow.
- Routine corpus passes that don't change the pass rate or surface a
  finding.
- Reformatting or commit-message-only changes.

When the change is borderline, lean toward updating — every entry costs
a minute today and saves much more on the next session pickup.

---

## Communication defaults

- **Ask clarifying questions before starting non-trivial work.** A
  two-sentence check on intent saves far more time than delivering the
  wrong thing completely. When scope or design is ambiguous, ask — do
  not assume.
- **Push back on decisions when there's a technical reason to.** The
  user explicitly welcomes challenge and questioning. State the concern
  clearly, explain why it matters, then defer to the user's call.
- **Name gaps between stated request and underlying goal.** If they
  diverge: *"You asked for X, but it sounds like the real goal is Y —
  should we do Y instead?"*
- **Think out loud on non-trivial decisions.** Share the reasoning, not
  just the conclusion. This lets the user catch wrong assumptions early.
- **Prefer dialogue over completeness.** Not every answer needs to be
  exhaustive — sometimes the right move is to pause and align.

---

## Trust but verify — universally

This applies to **all** sources without exception: external docs,
community resources, the agent's own prior knowledge, and the user's
stated assumptions. The user gets slightly less scrutiny than a
third-party blog post, but no claim is exempt.

Always seek a first-party authoritative source to confirm:

- `--help` output
- Live API calls
- Specification text
- Official documentation
- The corpus itself (for any format claim)

When a claim cannot be verified in the moment, **say so explicitly**
rather than acting on it.

For format work specifically: when a chunk-format claim conflicts
between the Petrolution spec, Mike Lankamp's MAXScript reader, and
direct observation of the corpus via `alo_dump` / `alo_roundtrip`,
**direct observation wins**. Both Phase 1 and Phase 2 corrections
came from this — the high-bit container flag (initial guess
inverted), the fixed-size 0x201 / 0x402 chunks (not described in the
spec). Trust the bytes.

### Log validated assumptions

When an assumption is confirmed against a first-party source, record it
where it will be seen again so the same check is never repeated. Valid
targets:

- This `CLAUDE.md` (for tooling-level conventions or working agreements)
- `docs/format-notes.md` (for format-spec confirmations)
- `docs/development-log.md` (for project-state decisions)
- Code comments (for why a non-obvious line exists)
- README (for cross-cutting decisions visible to all readers)

---

## PR-per-change workflow

Every commit reaches `main` through a PR. Branch protection enforces
this; the admin (DrKnickers) can self-merge once CI is green.

1. Branch from `main`. Conventions:
   - `phase/N-short-description` for phase work
   - `docs/<topic>` for documentation
   - `fix/<topic>` for bug fixes
   - `chore/<topic>` for routine maintenance
2. Commit with conventional prefix (`feat`, `fix`, `docs`, `chore`,
   `ci`). Body explains *why* and key decisions, not just *what*.
3. Push, open PR with `gh pr create --base main` and a Test plan.
4. Watch CI; surface for review when green.
5. Merge with `--rebase --delete-branch` (preserves the descriptive
   commit message; matches the repo's `required_linear_history`
   protection).
6. Sync local `main` (`git checkout main && git pull`).

Do not direct-push to `main` even though admin override would allow
it — the protection rules exist to give the audit trail value.

---

## What this is *not*

These principles are not license to:

- Lecture, moralise, or repeat the same concern twice after the user has
  acknowledged it and made a decision.
- Ask permission for every small step. Apply judgment: align on intent
  up front, then execute with confidence.
- Pad responses with caveats, disclaimers, or summaries of what was just
  done. Say what matters, stop.

---

## Quick reference

| Situation                                  | Default action                                   |
|--------------------------------------------|--------------------------------------------------|
| 3+ step task                               | Plan mode + TodoWrite tracking                   |
| Plan no longer matches reality             | STOP, re-plan                                    |
| Ambiguous scope or design                  | Ask, don't assume                                |
| Believe the user is wrong on a technical point | Push back once, defer to their call           |
| Stated request ≠ apparent goal             | Name the gap explicitly                          |
| Claim from any source, including own memory | Verify against first-party source before acting |
| Cannot verify in the moment                | Say so explicitly                                |
| Format-spec disagreement                   | Direct observation of the corpus wins            |
| User corrects you                          | Internalize lesson; fold into `CLAUDE.md` if general |
| Bug report                                 | Fix it; don't ask permission                     |
| Simple fix                                 | Don't over-engineer; skip the elegance pass      |
| Marking a task done                        | Quote proof. *"Would a staff engineer approve?"* |
| Phase ships                                | Update `docs/development-log.md` in the same PR  |
| Mid-phase notable change                   | Update the dev log if it shifts project state    |
| Routine work                               | Don't touch the dev log                          |
| Format-spec finding                        | Update `docs/format-notes.md`, link from dev log |
| Reaching `main`                            | PR + rebase-merge + delete branch (no direct push) |
