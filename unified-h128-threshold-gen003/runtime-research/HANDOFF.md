# Isolated gen003 threshold runtime

Research only; no production source, model, collector, package, or original build object was changed. Base native source is 4a2cf3dac9eb4eab0aab932307ca5096be953491. The only semantic change is the comparison threshold; H128, minimum 8, publication delay 8 and no maximum cap remain unchanged.

Dev runtime: `/home/bradley/glob2/artifacts/gradient-unified-20261019/threshold-sweep-gen003/runtime-research/runtime/bin/glob2`.
Set `GLOB2_RESEARCH_GRADIENT_THRESHOLD_MILLI` to 1000/4000/128000 for 1/4/128 direction-cost units. Unset defaults to 128000. Positive INT64_MAX disables model-triggered admission; cold/reactive/history initialization still work. Strict positive decimal input is parsed once per process, without signs or whitespace. Result metadata reports actual threshold and SHA256 of `research-gradient-threshold-input.json` (newline included).

`QUALIFIED-THRESHOLD-RUNTIME.json` is PASS: six frozen/research default runs matched world/snapshot/replay/final-save hashes; four observer CSVs also matched across binaries and threads. Lower 1000/4000 OFF/ON pairs matched simulation outputs. Unset/default and INT64_MAX metadata checks passed; nine malformed/zero/overflow values were rejected. Initial invalid fixture omitted required player arguments and was rejected by the CLI before reaching the helper; preserved failure log is superseded by the corrected valid-command invalid-environment tests.

Only AdaptiveGradientState.cpp and Headless.cpp were rebuilt into separate artifact objects. Exact ordinary-release compile/link argv and 644 reused input hashes are retained under `build/`. Original source remains clean and original ELF unchanged. No performance claim follows from these qualification runs: CPU affinity 4,5,8,10 shared the host with other work. The collection agent owns prospective sweep dispatch after all four warmups terminate. Saved-game `--ticks` is absolute, so use warm tick plus additional ticks.

The local evidence mirror excludes full runtime assets and source clones; complete private runtime/source remain on Dev. No production bundle was registered for this research ELF.
