# Independent renderer review

Three agents cross-reviewed backend contracts and implementations, selection/settings/capture/browser ownership, and content/terrain/build/packaging/documentation. The coordinator integrated changes and owns integrated acceptance.

Addressed findings:
- WebGPU atlas upload validation accepts zero offsets while retaining finite/bounds checks.
- WebGPU render target sampling honors the declared alpha representation.
- Completed WebGPU source allocations can be pressure-evicted; current unpresented work remains protected.
- Browser surface allocation failures stay inside switch rollback handling.
- Browser tests require the actual active backend, probe the presentation owner, and bound unresponsive worker inspection.
- Torus resource trimming preserves in-flight/capture leases, and density selection accounts for the complete layout and budgets.
- Guarded renderer artwork refresh before asset-toolkit initialization; the standalone switching fixture reproduces and now passes the regression boundary.
- Corrected HD pixel fixtures to inspect portable SDL before its documented post-presentation clear.
- Added live terrain pressure coverage and actual C++ WebGPU proofs; updated architecture descriptions and alpha comments.

Further findings addressed bounded producer continuation, explicit submission completion, preserved resource identity during admission deferral, exact physical scissor coverage, worker-owned WebGPU shader input, stale asynchronous preparation and fences, and torus phase rollback. Strict native cache-pressure cases passed on the prior coherent revision. Browser tracing showed the final shortcut in the repeated-switch fixture disabled correctly restored torus mode; the corrected sequence asserts restoration before exercising off/on input.

Final integrated release validation remains in progress after rebasing onto master c3cb01070. Browser recording completion and native menu-colony timing require focused diagnosis. Prior lower-level and application evidence does not qualify the refreshed final application revision.

Merged shader/core/native foundations: PRs 1115, 1116 and 1117. Live integration remains draft (1118). Defaults remain unchanged; unavailable physical platforms remain opt-in.
