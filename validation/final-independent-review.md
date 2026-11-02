# Independent final review

Reviewed commit dd38ad2902, `Cap Maxima operating carriers at eight per inn and
twelve per swarm`, against its parent. No actionable code, cleanup, compatibility,
or fixture-regeneration findings. No code fix is required by this review.

The eight-file change retains only the named 8/12 carrier ceiling helper at the
initial-request, planner-projection and operating-controller call sites; focused
regressions; documentation; SIM121 and its golden record; and the intentionally
updated existing legacy continuation expectation. Producers take precedence for
hybrid providers. Lower common and engine limits still apply. The existing
integrator, saved strategy/state layout, save format193 and minimum format floor
remain unchanged. Experimental birth, supply and retirement policy state and
format194 debt have been removed. Construction and upgrade crews remain separate.

The corrected integration fixture uses the catalog's only swarm tier and all
three inn tiers. It exercises saved requests20, planner initial/projected demand,
adaptive growth/release and a lower common ceiling. The standalone regression
also covers clipping an oversized saved request with zero measurable capacity.
The saved-request intermediate staffed threshold is not a blocker: the unchanged
controller return path caps each new ordinary operating request on its first
management pass, without rewriting pending saved orders or lifecycle crews.

I inspected `legacy-save-updated.log`, the earlier divergence log, the unchanged
repository checker and retained binary traces. The checker compares the midpoint
reload before allowing `--update-fixtures` to write new expectations. I also
independently recomputed all512 complete-record hashes from the retained run and
verified that they equal the committed expectation. All256 resumed records have
identical full record bodies and the expected MapHeader-version adjustment to the
aggregate. The first changed old expectation is tick30252;260 ticks change through
30511. Fixture SHA256:
`812c688a0b35a38665baffb620906f8ecd7342d2c96664e4a29d2392d240ef46`.
This is an intended AI-policy trajectory change, not evidence of lost reload
continuity. README correctly records the carrier policy in the active trajectory.

`git diff HEAD^ HEAD --check` passes. The working tree was clean at inspection.
Release Maxima and compatibility logs show zero failed tests (JUnit274 and90
cases respectively). These tests were run by the author; I inspected their logs
and independently checked the retained continuation bytes. I did not rerun engine
suites or establish additional platform coverage. Final revision-qualified build
and serial/parallel legacy verification were still in progress when this review
was recorded; their completion remains the author's validation responsibility.

Read-only review: no repository files modified. Only this authorized review
artifact was written.
