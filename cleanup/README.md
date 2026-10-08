# Shared-only growth cleanup

Source `7d35a0c812eb3be783e340c292e548969fa23cff`, based on master `37213ca3cb2dc806a491a77a525323464b96a859`. Master advanced only with checksum-reference repairs; no engine source changed relative to the performance baseline `68aa1075b`.

## Implementation and review

Removed the growth execution-mode CLI option, pipeline placement flag and the executor's OwnerOnly placement extension. The executor now differs from master only in its bounded growth queue capacity. Growth always submits to the same background executor; only zero workers invokes its existing owner fallback. `--compute-threads` counts the owner: 1 means zero workers, and 4 means three workers plus the owner. This corrects the earlier performance report's imprecise phrase “four compute workers”; its actual commands, core reservation and measured numbers are unchanged.

Delay remains 8. Delay setters only validate/assign and neither dispatch nor finish work. Tests cover both reserved and unfinished work, rejected delay changes, real shared execution, the zero-worker fallback and ordered publication with a gated earlier completion fence. There is no simulation/save/protocol change in this cleanup; the feature remains SIM34 / format149 / protocol67 / save floor58.

The independent read-only subagent reviewed the entire PR and cleanup, then signed off `7d35a0c81` with no blocking findings. That signoff includes the corrected lightweight unit-test reset and the source hygiene check. Review findings fixed include removed placement plumbing, side-effect-free delay setters, unused/misplaced includes, clear lifecycle/reference comments and stale original-deposit/owner-mode documentation. Legacy save readers and the immediate ecology/generator reference remain intentionally: they are compatibility/test code, not a second runtime execution mode.

## Validation

{
  "engine": {
    "passed": 602,
    "failed": [],
    "skipped": 45
  },
  "golden": {
    "passed": 13,
    "failed": [],
    "skipped": 0
  },
  "unit": {
    "passed": 889,
    "failed": [
      [
        "ImageAssets",
        "16-bit RGBA rounds normalized channels to the exporter reference"
      ]
    ],
    "skipped": 20
  }
}

The known ImageAssets native-SDL 16-bit decoding failure is independent of this change and already reproduced with master’s unchanged fixture without engine code; see the preceding final-master report. No other failure is accepted by this report.

Legacy-load reference traces were generated using the archived **pre-cleanup** executable `61b6ff740` and committed for SIM34. Both zero-worker and shared-worker runs of the cleaned-up executable must match those exact full sidecars, including the retained v108 checkpoint. The old executable and its hash are identified in `reference-binary.json`; generation did not use the new implementation to bless its own output.

All eight fixed 1,024-tick fixtures are additionally compared with pre-cleanup per-tick world/replay traces at compute sizes 1 and 4. `continuation.json` records each result. The updated benchmark runner also verifies delays 1/3/8 with compute sizes 1/2/4/8 on a 32-tick dense fixture. The removed CLI flag must be rejected. Golden verification runs without updating fixtures. Build, exact test commands, logs and JUnit are attached; the initial build was deliberately interrupted after the final API changes, and only `build-final.log` establishes the settled build.

This round is Linux x86-64 GCC15.2 only. No new Windows/macOS/Android/browser/threadless-build or display coverage is claimed. Zero runtime workers on Linux are tested, which is not a threadless-platform build. Existing performance measurements belong to pre-cleanup `61b6ff740`; no fresh throughput claim is made here. Old owner/shared ablations remain historical evidence, not supported commands in this head.
