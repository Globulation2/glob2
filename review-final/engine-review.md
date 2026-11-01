# Independent C++ review

Reviewer: `/root/engine_review`.

Reviewed the engine, statistics storage, result serialization, chart integration,
save/replay compatibility and live-player visibility. The initial review covered
the feature relative to `cbc20d2aabc364f7727439659001d29ffc741c25`; the focused
integration review covered `7241a3b51a` against master `80596f1477`.

## Findings addressed

1. Probability history originally checked value ranges and ordering without
   checking whether its ticks exceeded the loaded game counter. Game loading now
   rejects future samples at the owning Game boundary, alongside other diagnostic
   tick validation. A regression accepts a sample exactly at the saved tick and
   rejects an ascending sample one tick beyond it.
2. Result JSON serialization originally sampled and mutated statistics. The
   serializer now accepts `const Game&` and uses `const Team*`; missing or stale
   reporting endpoints are explicit errors. The three terminal command owners
   and the live/verifier comparison helper capture their own endpoint before
   serialization. Headless continuation saves still precede the final capture.
   A regression checks that serialization does not fill missing/stale history,
   then succeeds after explicit owner finalization.

## Integration conclusion

No additional runtime interaction defect was found in the focused merge review.
Formats 188/189 remain Maxima-specific; probability history starts at format 190.
The new TeamStats boundary regression checks that the pre-probability payload
loads without consuming a probability section in 188/189, while format 190
requires the section. Replay header coverage explicitly includes 188 and 189;
the existing replay floor 187 remains unchanged. The stale replay comment was
updated to identify probability format 190.

Maxima strategy migration still uses the actual source format. Warrior tactics
telemetry remains an independent snapshot observer and finishes before final
reporting statistics are sampled. Statistics storage remains owned by TeamStats,
collection by the Game/terminal owners, and presentation/serialization are readers.

## Verification and limits

`git diff --check` passed after the review fixes. No expensive build was launched
by this reviewer; the parent owns final native builds and test evidence. Relevant
regressions are `WinProbability/*`, `TeamStatsSave/probability*`, and the turn-engine
live/verifier comparisons. This static review does not establish cross-platform
execution equivalence or release performance. The parent is responsible for
regenerating the golden record for simulation revision 109 with the final binary.
