# Local Maxima development

This branch layers inspection UI, telemetry, experiment commands, optimizers,
and campaign tooling above the clean Maxima PR commit. Keep these commits local.
Make gameplay fixes on `codex/ai-maxima-pr` first, then merge that branch here.
Never merge this development branch back into the PR branch.

Maxima now uses AI ID **7**; Cortex retains upstream ID **6**. New schedules must
use the updated IDs. Historical protocols, receipts, and campaign documents are
preserved records, not validated inputs for this new engine. Re-freeze and qualify
new experiments before launching a campaign. No existing fleet was restarted.

The clean and development branches share the public Maxima save layout. Historical
private saves (including the old version-102 engine) belong with the preserved
`codex/ai-maxima-preserved-20260909` branch and its original build.

The frozen source is `6a207a01c415479147ba49fb63bd7e0a3b18f049`.
A verified history bundle, untracked/ignored asset archive, original working patch,
source/base identities, and validation logs live at:
`/Users/bradley/glob2-maxima-preservation-20260909`.

Build with `scons --build=build release=1 -j4 build/src/glob2`.
A bounded private smoke run is:

```sh
./build/src/glob2 -nicowar-scenario-match-nox maps/balanced.map 123456 2 7 2 0 0 1000 -nicowar-telemetry
```

`MaximaDiagnosticsTest` checks populated diagnostic sections and topology data,
then verifies that saving and resuming retains orders and simulation checksums.
The rebuilt full experiment fleet and all historical campaign suites have not
been qualified; the short smoke run is not tournament-strength evidence.
