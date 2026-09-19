# Maxima force estimates

New games use `recon.learned_force_enabled=true`. The fitted median warrior count
feeds the director's opponent assessments; fitted median combat power gates new
attacks. Own and enemy power use attack damage × attack speed × current health /
maximum health, with at least one power per living warrior. Counts and power round
to the nearest integer. Current visible forces remain a lower bound. The legacy
estimator remains available for old saves and explicit strategy overrides.

`AIMaximaForceModelData.inc` contains the frozen forward model's warrior-count and
combat-power forests. The unused total-population target is omitted. Each target
has four quantile forests (0.10, 0.50, 0.90, 0.95), each with 100 trees and at most
seven leaves. Integer thresholds and fixed-point leaves (scale 1024) avoid
platform-dependent floating-point decisions. Predictions are sorted across
quantiles and power is bounded below by warrior count. The median is a point
estimate, not a mean; upper quantiles are not claimed to guarantee a particular
combat success rate and do not drive the attack gate.

## Input contract

The first 13 features, in order, are visible warriors, remembered warriors,
warrior/force/building sighting ages, visible explorers, visible workers, visible
combat power, known buildings, visible buildings, reconnaissance confidence,
explored percentage, and AI tick. Ages are clipped to 100,000 at inference.
The next 12 features are previous value, historical peak, and change per 1,000
AI ticks for visible warriors, visible workers, visible power, and known buildings.
Negative change rates use floor division. The last feature is elapsed time since
the observation. The cutoff's other features remain unchanged during forecasts.

Each enemy's observation refreshes on the first strategic review and then after
at least 1,000 AI ticks, matching the training cadence. Strategic reviews forecast
to the present; intervening lightweight samples retain that prediction while
enforcing the current visible-warrior lower bound. Only the existing fog-gated
scan supplies inputs. No enemy identity, hidden units, or global truth enters the
model. Disabling `recon.force_memory_enabled` retains the visible-only ablation.

## Provenance and compatibility

The model was frozen in research commit `966eb8312`, from 336 games across 20
geographies. Export SHA-256 (uncompressed JSON):
`7e8fdbc8b2c0b2b413d1203574f5b23e7e61a9744e0633d6a3574d9ebbf8e6eb`.
Training used 100 iterations, seven leaves, minimum 30 samples per leaf, L2=2,
and seed 19. The subsequent 72-game, 24-geography holdout measured current-count
MAE 5.90 versus 7.19 for the legacy estimator; 5,000-tick forward-count MAE was
8.09 versus 9.34 for holding the fitted present estimate. Those are historical
offline prediction results, not evidence of a live win-rate improvement. Research
datasets, renderers, campaign scripts, and truth-audit telemetry are not runtime
dependencies and are not retained in this branch.

Save format 111 persists the observation history and cached forecast. Pre-111
saves without the new strategy key retain the legacy policy; the save floor
remains 58. Network protocol 36 prevents mixed AI decision policies. The replay
floor remains 99 because recorded order interpretation is unchanged.

`MaximaForceModelStandaloneTest` checks golden fixed-point predictions and causal
history; `MaximaCombatIntegrationTest` checks fog isolation, director consumption,
the live power gate, and full AI save/load across multiple model observations.
`MaximaDiagnosticsTest` verifies saved-game orders and per-tick simulation checksums.
