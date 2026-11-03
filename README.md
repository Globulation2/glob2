# Rated platform stack smoke argument repair

Tested revision: bc130d818fea54c76eb6e5527a4c14d4721359d4. Base/runtime image source: 4db5658787434696c50fb92d9adcbcc32919b960. The sole change is host Python --output-dir to --out. Identical previously built image IDs are retained. Node 22 and the pinned Python validation environment were used. Stock repository glob2-engine and glob2-ai-music AppArmor profiles were loaded; confinement remained enabled.

Original full image build passed the first eight stack checks and reproduced the parser failure. The fixed complete --no-build --match-e2e run passed all nine checks, including a real sandboxed map job, registered accounts, rated queue, two real engine clients through WSS relays, uploaded match record verification and rating/history validation. Both clients exited 0 without desync. The deployment smoke unit test passed; diff --check passed. Runtime source, dependencies, simulation and compatibility inputs are unchanged.

Archive contains original and final commands/status/logs, final match results and exact image IDs. Assignments/tokens and temporary deployment credentials are excluded. Full hosted matrix and browser recovery remain separate pending work; this focused integration is not a full hosted checkpoint.
