# Gradient organization PR evidence

This branch attaches selected generated files for PR #403. They are deliberately
kept out of the source PR. The source branch is `codex/gradient-organization`.

## Before/after simulation comparison

The `gradient-runtime-before2` executable came from the first cleanup commit
(`401b93ee1` before rebase); `gradient-runtime-after` came from the second
cleanup pass (`62561c37a` before rebase). Both loaded the same
`gradient-pipeline-evidence/fixture/initial.game.gz` and ran 1,024 ticks with
one gradient worker, eight-tick delay, replay output and checksum telemetry.

The two `game.replay.checksums` files are byte-identical (SHA-256
`a7e337cbd75a21fcadad8f669c29c72241233a11efe6c8d09bdf94ba7d4df2b8`).
The two `game.replay` files are also byte-identical (SHA-256
`8014f035e8e3c5ff11eb600c1e65e7f9d8014f5f36a4515d2e177d37ddd1f0ae`).

## Old-save continuation

`gradient-pipeline-evidence/checkpoint-{0,7}/final.game.gz` were created
before runtime encapsulation, with gradient jobs pending. The corresponding
`gradient-runtime-old-save/resumed-{0,7}/game.replay.checksums` were produced
after runtime encapsulation. They exactly match the older build's
`gradient-pipeline-evidence/resumed-{0,7}-1/game.replay.checksums`:

| Phase | Matching SHA-256 |
| --- | --- |
| 0 | `dfb3b542a465e51f2b10997e258bc57f5ea3bcc67f767e48eb5734342f29ce3e` |
| 7 | `4a39b6e7300dcb4a9bb06666177727a5a80c56d9215bd403075ed813f8986a8c` |

## Rebasing onto current master

After rebasing onto `300ded8d6`, `test/check_gradient_pipeline.py` passed
same-schedule trace/outcome comparisons, all eight save-continuation phases,
and invalid-option rejection. The resulting fixture, uninterrupted checksum
trace, and phase 0/7 checkpoint saves are under `gradient-merge-evidence/`.
The full generated run remains local because it includes many duplicate files.

This is execution evidence on macOS. Android CI compile coverage is reported
on the PR; equivalent per-tick execution across operating systems was not run.
