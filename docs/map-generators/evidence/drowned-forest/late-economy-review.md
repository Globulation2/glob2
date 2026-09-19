# Final16 seed19 economy review

Read-only save exports `final19-*-{initial,final}.*`, farm analysis `final19-farms.py`, late analysis `final19-analysis.py`, and log extraction `final19-log-review.py` preserve this review. Logs and games are from `../final/play-19-*`. The existing `/tmp/inspect-save` loaded the saves without advancing simulation. No tracked files changed.

## Initial actual farm components

| Seat | Swarm | Farm tiles | Wheat tiles | Fertility sum /65536 | Wheat-frontier steps | Reachable frontier tiles |
|---|---|---:|---:|---:|---:|---:|
|0|216,160|167|108|19.99|2|86|
|1|214,87|185|108|16.44|5|81|
|2|231,19|115|106|6.89|3|76|
|3|13,229|167|108|9.53|2|84|

Fertility sums cover the actual connected grass/wheat farm component, not arbitrary grass within a radius. These are comparative growth-potential units, not a calibrated food-per-tick rate. Initial frontier measurements use eight-neighbor access; late inn/home distances use cardinal building perimeter roots and eight-neighbor walking, with resources/buildings blocking passage.

## Maxima's two worst failures

Rotation0, seat3: 38 worker births,16 workers starved,1 worker killed. Wheat harvesting grows320@15360 to341@16384,398@20480 and418@25000. Its inn count drops from1 to0 at15872 and returns to1 at16896. At final save the only inn is at81,205:214 walking steps from the home swarm and213 from that home's wheat frontier; it has1 wheat and0 assigned workers. The swarm remains at13,229, has0 wheat and1 assigned worker against14 requested. The home farm still contains49 wheat tiles and118 empty grass tiles, with no buildings covering the farm and a2-step wheat haul. This is remote/lost feeding infrastructure with reachable wheat remaining, not complete farm depletion. Final save and aggregate timeline cannot establish whether the previous inn was demolished by AI policy or destroyed in combat.

Rotation3, seat0: strongest initial farm (19.99),28 births,15 workers starved,7 workers killed. Its inn disappears by17408, swarm by17920, barracks by18432, school by18944. Worker starvation begins after the home loses these structures. Several later inn attempts do not survive. Final home farm still has95 wheat tiles,72 empty grass tiles, no buildings in it, and a2-step haul from the original swarm site. Combat and lost infrastructure are substantial confounders; the farm is neither weak nor exhausted.

Both failures have growing military populations before the food crisis (seat3 reaches17 warriors;seat0 reaches21), so demand/AI production policy also matters. This is observational evidence, not a controlled causal attribution to overproduction.

## Cross-AI and weaker-farm comparison

The same seat3 supports other AIs with nearby feeding infrastructure: Nicowar's surviving home inns are0–4 steps from its remaining farm; Cortex has full inns (30 and48 wheat); Cabino ends with four local inns and1 worker starvation. Seat0 supports Nicowar with multiple stocked inns and1 worker starvation, despite covering24 farm tiles with buildings.

The genuinely weak seat2 farm remains at20–29 wheat tiles in all late saves. Maxima there has8 worker starvation, with two near-home inns containing0 wheat and1 assigned worker each. Cabino has1 worker starvation, Nicowar and Cortex have0. Lower growth can increase pressure here, but surviving nonempty crops and staffing differences mean we cannot infer a required fertility threshold from these results.

## Recommendation

Do not treat a pond enlargement as a demonstrated fix for the two largest starvation episodes: one occurs on the strongest farm, and both lose local feeding infrastructure. A minimum renewable-growth criterion is defensible as a fairness contract because the initial farms differ by almost3x, but its numerical threshold needs calibration. If adding one, correct only low-potential farms with a bounded shore/pond adjustment and rerun the same rotations. Keep the report explicit that AI feeding/expansion choices and combat remain involved; do not claim the generator guarantees zero starvation.
