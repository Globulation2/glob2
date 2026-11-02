# Independent subagent review

Reviewer: retirement_control (read-only), independent of implementation.
Base: 9ece139a26b84caec0045dc688817315b19fe08b.

No actionable source correctness or scope findings. The shared semantic ceiling applies to initial requests, planner carrier projections and normal operating updates. Producers, including hybrids, receive12; feeding interiors receive8; lower common and engine assignment limits still win. Construction and upgrade crews remain separate. Serialization, strategy fields and save/network versions are unchanged; existing saved controllers adopt ceilings on their next update without rewriting pending orders. The existing controller's averages/cooldown and final clamp remain intact.

The reviewer requested real validation before merge: focused and broad Maxima tests, legacy-save continuation/save-reload equality, replay/network acceptance, SIM121 and regenerated golden record. Those results are supplied separately, not claimed by source review alone.

During development, the new test incorrectly assumed upgraded swarm tiers exist; only inns have three tiers. The fixture now uses the single valid swarm tier for each of the three inn-tier checks. This was a test-setup correction, not a policy change.
