# Proposed collaboration policy

**Status: draft for maintainer discussion; not an adopted project policy.**
This proposal needs agreement from the maintainers before it is merged. Its review
rules apply to this policy PR too. The questions below are deliberately unresolved.

## Scope: preserve the game

Preserve Globulation 2's existing engine architecture and fundamental gameplay.
Do not include major architectural changes, engine rewrites, or changes to basic
gameplay mechanics in this contribution workflow. New features must fit within
those boundaries.

Fix demonstrably broken engine behavior. A bug fix should identify the intended
behavior and show how the current code violates it. Crashes, invalid saves, and
unintended simulation divergence are examples. Changing balance, unit capabilities,
economy, or victory rules is not a bug fix simply because the author prefers the
new result. When the distinction is disputed, stop at a discussion PR.

Changes to save compatibility, replay behavior, or network compatibility must be
called out explicitly. Do not silently discard compatibility as cleanup.

## Review and merging

| Change | Proposed review requirement |
| --- | --- |
| Focused, reproducible bug fix | A separate reviewer; an AI may review and merge autonomously after the evidence and checks pass, with no unresolved objections. |
| New feature or functionality that preserves the fundamental game | Explicit approval from a second human before merging, plus the relevant checks. |
| Major architecture or fundamental gameplay change | Outside the accepted scope of this policy. |
| Collaboration policy or review-rule change | Human maintainer agreement before adoption. |

Never merge your own PR. An author running another review pass on their own work
is useful testing, but is not the separate review and merge step. Do not use a
second AI session as a way to bypass this rule or impersonate a second human.
The precise identity/account boundary for an independent AI reviewer is an open
question below; do not assume that spawning another agent settles it.

Keep PRs small and focused on one problem. Separate unrelated cleanup, features,
and refactors. Describe dependencies and the intended merge order for related PRs.
Disclose AI assistance and keep a human contributor accountable for the submission.

Read issue discussions, PR comments, reviews, and relevant maintainer decisions
before merging. Hold a PR with an unresolved technical or design objection. Resolve
the concern in the discussion; a green build or an AI opinion does not overrule it.
Recheck the current PR head, review state, and checks immediately before merging.
Do not bypass branch protection or dismiss another person's review to land a PR.

## Minimum evidence for bug fixes

1. Record an observable failure and its expected behavior. Provide reproduction
   commands and a minimal fixture where possible. Distinguish a reported bug from
   one reproduced on the current base revision.
2. Add a regression that fails on the unfixed code and passes on the fix when
   feasible. Exercise the real affected behavior rather than copying the new
   implementation into a test. If an automated regression is impractical, explain
   why and provide repeatable manual steps and before/after observations.
3. Run the relevant regression and required CI checks on the final code. Test the
   affected platforms and build configurations; document anything not tested.
   Use sanitizers when memory safety or undefined behavior is implicated.
4. For simulation changes, verify repeatability using the same initial state,
   seed, settings and orders. Compare relevant state or order traces, not merely
   the winner. Check save/load continuation, cross-platform determinism and network
   compatibility when affected. A deliberate bug fix can change old outcomes;
   describe that change instead of claiming equivalence to the broken behavior.
5. Attach a concise verification comment before merging: exact tested commit,
   commands, platforms/toolchains, results, fixture locations and remaining limits.
   Distinguish fresh compilation from running a transferred binary, and automated
   checks from manual playtesting. Revalidate affected checks after further edits.

Large AI tournaments can expose rare combinations of maps, teams, formats and
settings. Retain enough information to replay a failure: source revision, build
configuration, platform, seed, map, team/AI assignments, settings, initial save,
orders or replay, and logs/checksums relevant to the failure. Reduce a tournament
failure into a focused reproducer where possible. Large run counts alone do not
establish correctness, and agreement among AI reviews is not experimental evidence.

## Questions for maintainer agreement

- What establishes a separate AI reviewer and merger: a different contributor,
  a dedicated bot account, or another explicitly agreed mechanism? How is the
  responsible human recorded?
- Does feature approval need one maintainer other than the author, and must that
  person also perform the merge?
- Which CI jobs and platform checks are mandatory for each category of fix?
- How should exceptions to automated regression requirements be approved, and
  where should larger reproduction artifacts be retained?
- Are these scope boundaries and the treatment of disputed bug fixes acceptable?

Once agreed, update this document and AGENTS.md together, remove the draft notices,
and consider a separate PR for any repository enforcement settings. This proposal
does not itself change GitHub permissions, required approvals, or CI protection.
