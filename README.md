# Nicowar dig-out regression evidence

Implementation branch: codex/fix-nicowar-dig-cycle. This branch holds ignored development evidence only.

The formerly OOM-killed cached-consumer seed completes32768ticks under4GiB after replacing only Nicowar Attack.cpp;1/4participant per-tick traces match. See regression-verification.json and regression-evidence.tar.gz for input, source, exact compile/link/run commands and traces. These isolated controls do not replace a fixed-cohort attempt and are not a clean production build.

Mac evidence:87final-head engine/protocol/replay cases, plus80earlier strategy/continuation cases on identical production Attack.cpp. The default macOS engine build fails in unrelated MCPTest; the ignored build wrapper excludes that fixture and selects unit tests with their required platform stubs. Logs preserve failed wrapper attempts and the final successful commands. No Windows/browser or macOS release claim. Linux release verification is in progress.

Original failure/debugger evidence remains at https://github.com/Globulation2/glob2/pull/1161#issuecomment-6149576927 .
