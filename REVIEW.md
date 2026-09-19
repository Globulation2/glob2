# Independent map review rounds

The same map-review agent reviewed source and retained native previews repeatedly.

1. Corrected doorstep routing inside the protected home mask; switched to final-world route measurements after resource placement.
2. Added real building clearings at shortcut destinations and verified that every landing/clearing belongs to the colonies’ initial walk network. Replaced round plot stamps and repetitive lake capsules with irregular shore fields and hooked/S/forked lakes.
3. Added final stone/algae access floors, protected orchards from room reservations, and checked that cutting and swimming retain complementary benefits. Multiple designated portages now serve full maps.
4. The reviewer identified Cortex’s sustained food stall. A 300% initial wheat test did not cure it. Enlarging renewable wheat fields raised growth potential and changed Cortex from 10 births/45k ticks to70 on seed1, without increasing engine resource growth rates.
5. Reviewed round5.png and round5-final.png: crooked lake country now reads clearly; field growth remains contained and road/woodland character survives the game. The reviewer caught one shared algae pool on ALL64-side rectangles; pooling is now restricted to64x64.
6. Reviewed bounded landscape reconstruction: copied proposal contexts and restored simulation RNG preserve deterministic replay; validator identifies the landscape using ordered starts plus exact undermap. Added optional neutral-bay trails to one/two-colony maps to give Extra trails a meaningful effect where the home graph has no spare edges.

Translation review used a separate agent. All33 catalogs were reviewed/edited; five translation tests and the structural/key checker pass. Internal world-validation diagnostics retain the framework’s raw-English convention. Native-speaker review of every language has not occurred.

The reviews are implementation feedback, not independent maintainer approval or human playtesting.

7. The random sweep exposed a real iterator lifetime bug: restoring `L` after a failed neutral bay freed the candidate vector being iterated. The reviewer identified it independently; the loop now iterates immutable `base.candidates`. Release repros for seed100034 previously exited on SIGBUS; repaired generation succeeds. Seed100078’s inconsistent terrain reconstruction also succeeds after the same fix. A retained regression checks repeated whole-map fingerprints on that crowded request. LLDB’s different allocation pattern did not reproduce the signal; its normal completion is not claimed as sanitizer evidence.

8. Replaced colony-count portage quotas with an area-based target: two full-size cuts guaranteed, up to four on larger maps where useful endpoints fit. Every cut remains complementary after the other options are enabled.
9. Investigated compact Cortex stalls. A seam-margin experiment did not fix them and was removed. The reviewer identified the actual inn adjacency constraint: outside building room cannot touch grain across the containment border. Larger compact wheat fields, a 4×4 opening court and a short unseeded entrance allow early construction. Both checkerboard parities and final worker access are checked. Productive fertility excludes the court and entrance. Unused courts may grow over; this is an opening promise.
10. Reviewed the control study: no wholly dead control; algae saturation and small stone rounding documented. Added renewable-timber telemetry to distinguish it from structural forest. Higher compact sowing was tested through wheat300 before changing its baseline.
11. Reviewed the profile fixes: exact local fertility windows and endpoint-terminating BFS. Fifteen paired requests had identical terrain/resource/space/fertility outputs. Final review found no blocking defect; its small suggestion to separate opening-access validation from the telemetry pointer was implemented with an explicit flag.

12. Continued compact/narrow playtests and reviewed inn discovery. Static worker connectivity was insufficient: a far-end court remained in fog. A saved-state inspector proved both parity sites were legal when revealed and remained legal through4096 unattended growth ticks. Long compact courts now face home; haulers discover the rim. All four rotated Cortex starts construct inns early; the late narrow economy limit is recorded rather than hidden.
13. Reviewed the completed777-control/436-envelope study, documented saturation and proposal switching, and flagged near-exhausted retry margins in crowded narrow all-low cases for final reruns.
14. Reviewed candidate19 code, initial/final native previews, and four narrow rotations. Found no scoped correctness blocker in budget-preserving re-sowing, court selection or notch routing. Source findings and remaining late-game starvation are recorded in review-r19.md. Final translation pass added the separate-lakes design failure key to all33 catalogs and passed the strict audit again.
