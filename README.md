# Vulkan gradient correctness evidence

Candidate: 389b821843ebda0492c88794ebefc93f5e209b45. Base/latest fetched master: 277510840054b1416f2defd7d86fe1431dd0ddc0.
Linux x86_64, GCC 13.3.0, NVIDIA GeForce RTX 3070 Ti Laptop GPU, NVIDIA 580.178.04. Vulkan driver integer 2435612928; shader f628549a478f612d15c729e828f030e3f9897c32031af487b76f01fa3fa0456f.

All builds here use dev_fast=1 (-O0 -g1), the existing pinned SDL3 prefix, and unchanged shaders. This is correctness evidence, not release CPU-offload or runtime qualification.

## Results and commands

- `GLOB2_SDL3_PREFIX=/home/bradley/glob2/build/sdl3/prefix python3 tools/dev_build.py --build=build/gpu-offload-correctness unit-tests engine-tests build/gpu-offload-correctness/src/glob2`. Final commit build: commit-build.log.
- `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json GLOB2_REQUIRE_VULKAN=1 GLOB2_TEST_ARTIFACTS=$PWD/artifacts/gpu-offload/commit-unit-proofs build/gpu-offload-correctness/test/glob2-unit-tests -ts=RuntimeGradientBackend,GradientPreparationContract,OpenCLGradient,GradientPipeline,PathGradient,GlobalGradient,ComputeExecutor,FieldGradient`. Final commit: 152 cases / 704945 assertions passed (commit-unit-nvidia.log).
- `GLOB2_BUILD_DIR=$PWD/build/gpu-offload-correctness VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json python3 test/run_tests.py --binary engine --no-display --filter 'GradientBackendSwitch/*' --filter 'GradientPreparation/*' --filter 'ReplayStepCounter/*' --junit artifacts/gpu-offload/commit-engine.xml`. 30 cases passed; selected inventory is in the XML. ReplayStepCounter is a unit suite and was not selected by this engine invocation.
- `python3 tools/test_gradient_proof.py`: four parser/oracle tests passed. Six pinned shader-contract tests and documentation checks passed. See commit-proof-tool-tests.log, commit-shader-contracts.log and commit-docs-check.log.
- `GLOB2_CLI_BINARY=$PWD/build/gpu-offload-correctness/src/glob2 PYTHONPATH=test python3 -m unittest test_cli_smoke.CliSmoke.test_match_verification_preserves_trace_across_compute_sizes`: passed.
- run_device_storage_matches.py: CPU baseline plus Vulkan Frozen8/Jacobi at counts 1/2/4/8; eight eight-slot stress repetitions per algorithm. All 23 runs passed the exact committed golden trace. 8458 GPU fields committed in the Vulkan multi-slot runs. Each one-slot run reported zero GPU commits. Final commit rerun: run_commit_matches.py, nine matching traces with nonzero multi-slot GPU commits.
- run_continuations.py and run_roundtrip_continuations.py: 24 runs spanning CPU/Frozen8/Jacobi and counts 1/2/4/8, from a retained save and then a newly GPU-produced save. Each cohort had identical per-tick checksums and byte-identical final saves. Multi-slot Vulkan runs committed GPU fields; one-slot runs remained CPU-only. The scripts retain exact commands and input paths.

## Diagnosis and retained failures

The original three captures are extracted from origin/codex/runtime-gradient-merge-evidence. Independent heap analysis reproduces 451 below-oracle Frozen8 cells and 139/810 above-oracle Jacobi cells. The current master path reproduced 1415 above-oracle Frozen8 cells. A stronger mapped-read fence did not solve it: Jacobi still produced 371 below-oracle cells. Both new captures are permanent regression inputs too.

Upload roundtrip stress also caught a cost-buffer mismatch at word 9216 before shader relaxation (transfer-repeats/5.log). Additional evidence capture now preserves expected and actual transfer words rather than only the first failing index. The MFENCE experiment was rejected; its source and failed capture are retained. Device-local compute buffers and separate cached-preferred coherent transfer/control memory avoided the observed failures in the retained stress runs. This does not establish a universal driver defect or prove all host-coherent storage configurations invalid.

## Limits and next work

No release timing, CPU cost ranking, CPU savings, driver/application CPU split, publication-wait distribution or platform enablement is established. GUI live switching and actual device-loss injection are not covered by these runs; CPU/shared scheduler tests cover switching, cancellation, rollback and publication contracts. Windows, Apple, mobile and actual threaded-browser WebGPU execution remain unqualified. CPU default settings and preparation remain unchanged. Resident preparation, resource pools and solver orchestration are separate work.
