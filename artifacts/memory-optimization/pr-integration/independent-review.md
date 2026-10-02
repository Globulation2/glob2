# Independent review of memory optimizations

Reviewed PR #577, `origin/master...9bf480754`, covering all 36 changed files. Read-only review; no source edits or GitHub messages. This includes the current-master rebase adaptations for `ThreadSupport::launch`, 16-team growth coverage, cooperative initialization, gzip persistence, compact Maxima fields, shared terrain/geometry, and benchmark tooling. Parent owns implementation and verification.

## Must fix before merge

### P2: Own each block before growing its pointer table

`libgag/include/ChunkedStreamBackend.h:100–104`, original expression `blocks.emplace_back(new unsigned char[blockSize])`.

The 1 MiB array is allocated before vector reallocation. If pointer-table allocation fails, `emplace_back` never constructs its owning unique_ptr, so the array leaks. This occurs exactly in the low-memory scenario the persistence failure boundary is intended to handle. Acquire an owning local `unique_ptr<unsigned char[]>` first, then move it into the vector. Preserve `new unsigned char[blockSize]` without value initialization. Parent has already applied this fix after review feedback.

Useful regression: force pointer-table allocation failure after array allocation, check array deallocation, then verify a later save succeeds and the previous file survives. Throwing bad_alloc from a serializer is valuable boundary coverage but does not exercise this ownership transition.

### P2: Own the inflated backend before allocating the input stream

`src/EngineLoaders.cpp:27`, original expression `make_unique<BinaryInputStream>(glob2OpenMapOrSaveInputStreamBackend(...))`.

The backend factory returns a raw owning pointer before `make_unique` allocates the stream object. If that small allocation fails, the catch returns load failure but leaks the entire validated snapshot, potentially ~1 GiB. Hold `unique_ptr<StreamBackend>` locally, construct the BinaryInputStream with `backend.get()`, and release the temporary owner only once stream construction succeeds. BinaryInputStream's constructor simply stores the pointer and its destructor deletes it (`libgag/include/BinaryStream.h:68–69`). Adjacent standalone header readers use the same older idiom; applying the same mechanical safeguard there is reasonable.

Useful regression: inject stream allocation failure after the backend exists, using a destructor-counted backend or allocator fault hook, and verify ownership is released before returning failure.

## High-value comments and small cleanups for this PR

### Explain the memory budget at the chunk abstraction

`libgag/include/ChunkedStreamBackend.h:15–35` already states that block-table growth cannot copy payload bytes. Add two concise reasons: snapshots may exceed a gigabyte; a growing contiguous buffer temporarily retains old and new allocations, and flattening recreates that peak. State the invariant: blocks remain uninitialized until explicitly written; only committed bytes are readable; normally at most one partial block remains. Avoid suggesting `make_unique<T[]>(n)` casually: it value-initializes the payload and adds a complete zeroing pass.

Explain that `tableCapacity()` measures pointer-table allocation separately from payload capacity. This makes the acceptance formula intelligible to future callers.

### Explain lifetime ordering, not just container choice

`src/gui/GameGUIStep.cpp:386` and `libgag/src/BackgroundFileWriter.cpp:150` already have the right short comments. Expand the autosave comment to make the failure mode concrete: capturing before the wait can retain an active, queued and newly captured snapshot simultaneously. Worker release-before-idle is a memory ownership contract, not merely a status notification convention.

`src/EngineLoaders.cpp:22` should say header preflight used to repeatedly inflate the same complete file, and that ownership stays within one load operation. `src/EngineInit.cpp:399` correctly releases the snapshot before replay/network initialization; retain that explicit lifetime comment. No process-wide save cache is needed.

### Document the gzip compatibility traps beside the code

`libgag/src/FileManagerGzip.cpp:151` correctly forbids flush between input ranges. Keep this explanation next to the compressor, and specify that input chunk boundaries are storage choices, not save-format boundaries. The level-zero comment is important: do not accidentally delete the whole-buffer fallback while treating it as another avoidable allocation. The optional path exists for exact stored-block byte compatibility, outside the default-save peak budget.

`inflateBackend` would benefit from a short comment at its successful return explaining why validation must finish before any header/body read: CRC errors, truncation and trailing/concatenated data must reject the entire input. This also explains why loading does not merely stream straight into mutable game state.

### Make compact numeric bounds auditable

`src/ai/maxima/AIMaximaFarming.h:57–58` has no explanation for narrowing the temporary convolution planes. Add the numeric proof next to those vectors or `buildWaterConvolution`: a binary water mask produces successive maxima 16, 256, 4096, 65536; only the intermediate planes are uint16, while the final fertility remains uint32. This is particularly valuable because 65536 is precisely one beyond uint16.

`src/ai/maxima/AIMaximaPlacement.h:207–208` should state that resource amount matches the engine's byte-sized resource amount and resource type retains the signed empty sentinel. `foodOpportunitySourceCache` is a copy of `WorldTile::foodOpportunity`, which is already uint32; its old uint64 wire encoding remains deliberate.

`src/ai/maxima/AIMaximaDistanceField.h:10–11` already explains the wrapped-Manhattan bound and INT_MAX translation. Add that obstacle-constrained route distances must stay wide: a route can be far longer than the map's geometric diameter. The existing code correctly leaves routeDistanceCache and routeParentCache alone.

`src/map/MapStep.cpp:180–182` has an excellent overflow explanation. Name the assumptions (smallest 16x16 map, maximum 32x32 footprint, radius32, Building::MAX_COUNT1024) so future map/building limit changes trigger review. Prefer compile-time bound assertions when the footprint/map limits have a shared authoritative constant; do not introduce a duplicate magic maximum solely for an assertion. The 16-team rebase retains band-major Uint16 count vectors and uses a valid 48-bit packed mask (3*16); I found no packing error.

### Explain shared-cache ownership and unusual loaded state

`src/ai/maxima/AIMaximaPlacement.cpp:20–38`: comment that dimensions determine the complete immutable nine-neighbor geometry and weak_ptr entries prevent a process-wide cache from retaining former maps forever. Preserve traversal order and duplicate wrapped neighbors on small maps. At load-time canonicalization (`3519–3526`), explicitly say equality is required because saved noncanonical cache content must retain its original bytes and continuation behavior.

`src/map/Map.cpp:102–113` and `src/map/Map.h:416–424`: explain the threading contract. The mutex serializes creation of the shared snapshot; it does not currently protect all terrain writes (`tile.terrain` is written after unlocking). If concurrent terrain edits are unsupported while executor jobs initialize snapshots, document that established invariant. If such edits become supported, assignment and invalidation must be synchronized with initialization. This is a conditional future concern, not an established current race regression. Resumed searches correctly retain their old frozen shared_ptr and new searches obtain the new classification.

### Reduce dense or duplicated mechanics without adding a framework

Normalize indentation in the newly added persistence code (several files mix existing tabs with new four-space blocks). Expand multi-operation lines in error-sensitive code, particularly append/commit, zlib setup and worker handoff. Use meaningful names such as `snapshotBackend` instead of `memory` now that the implementation is chunked. These small edits improve reviewability without introducing behavioral risk.

The two DeferredGameSHA1::apply implementations duplicate the original-header substitution logic. A small private range-driven helper can centralize the hash sequence while keeping distinct storage adapters and patch operations. More important than eliminating 15 lines is a comment: hash the original header captured before backpatching, then patch the finalized hash. Hashing the final header would change legacy bytes.

`legacyVector` passes an unused decode argument in writer variants and an unused encode argument in readers. This permits a shared executionState call site, so it is not inherently wrong; add a brief rationale or use clearly named adapters. Do not replace it with a general serialization framework in this PR.

## Optional robustness follow-up

The legacy `gzipCompress`/`gzipDecompress` helpers (`libgag/src/FileManagerGzip.cpp:60–112`) call deflateEnd/inflateEnd only on normal return. output.resize/append can throw and leak zlib state. This predates the new chunked path, which already uses scope cleanup; using the same RAII cleanup in these helpers would tighten the level-zero compatibility path and tests without changing bytes.

`writeGzipAtomicImpl` cleans up std::exception failures, but a nonstandard exception from encode bypasses fclose/unlink. Current internal encoders normally throw standard exceptions, so this is not a demonstrated normal-save bug. An owning FILE/temporary-file scope guard would make cleanup independent of exception type and simplify future extensions. Avoid broad atomic-file infrastructure refactoring just for this change.

## Verification assessment and limits

The retained evidence is unusually strong: late-game peak 3.959→1.927 GiB against the preceding optimized build, byte-identical gzip comparisons including level-zero, native phase measurements, seven paired CPU runs per early/mid/late fixture, 438 tests and 3,072 detailed continuation ticks. I read the reports and relevant harnesses; I did not independently reproduce the long benchmark or native build. Parent is validating the rebased current-master integration, so older binary hashes must not be represented as measurements of the rebased executable.

Fault coverage should distinguish throwing bad_alloc deliberately at a persistence boundary from failing an actual allocation inside an ownership transition. The two must-fix leaks show why both matter. CPU and memory measurements are correctly separated; interleaved processes remain resident together and cannot establish single-process peak RSS. The seven-pair bootstrap result is empirical evidence for these fixtures, not a universal performance guarantee. Linux/Windows execution and interactive play remain explicitly unverified.

No other concrete correctness defect was found in compact distances, retained legacy wire widths, shared geometry, growth counter removal-before-addition, validated gzip loading, or single-snapshot autosave sequencing. Keep the comments tied to invariants and failure mechanisms; avoid narrative comments listing today's benchmark percentages in engine code.

## Resolution recheck

Re-read the parent's uncommitted follow-up diff after the initial review. Both required ownership fixes are resolved:

- `ChunkedBuffer::ensureCapacity` now owns each uninitialized array in a local unique_ptr before pointer-table growth; a vector allocation failure releases the array automatically.
- `openOwnedGameStream` retains unique ownership of the validated backend until BinaryInputStream construction succeeds, then transfers ownership. The helper is used by the operation-wide input and both standalone header readers.

The new comments accurately describe the contiguous-copy peak, uninitialized/committed payload invariant, separate pointer-table budget, header preflight lifetime, original-header hash substitution, gzip validation barrier, four-pass fertility maxima, route-distance exception, compact resource widths, weak geometry ownership, exact loaded-cache canonicalization, and autosave wait-before-capture ordering. I found no inaccurate bound or new correctness issue in these edits. The water-snapshot comment appropriately distinguishes cache locking from the existing simulation scheduling constraint on terrain mutation, without claiming the mutex protects terrain writes.

The optional older-gzip RAII, formatting and generic cleanup suggestions remain follow-up recommendations, not unresolved blockers. This resolution check was source review only; parent owns rebuild/test verification and final evidence.

## Current-master hardening integration review

Reviewed the newly merged untrusted-input hardening (`7711d9da8`) on `origin/master` (`60081b4f2`) while the parent resolved rebase conflicts. This is a narrow compatibility assessment, not a review of the whole security PR.

1. The new `MAX_COMPRESSED_GAME_FILE_BYTES` 256 MiB constant is reused as the expanded-output bound in both master gzip loaders. It rejects the validated late snapshot (1,023,964,056 bytes). Keep the compressed-input cap; use a separately named, explicit 2 GiB expanded-input cap for chunked direct inflation, documenting the compatibility reason. The chunked backend still needs full CRC/truncation/trailing validation before exposure. At the exact expanded cap, permit gzip trailer consumption using a one-byte scratch probe and reject if any output is produced; do not allocate another payload block or accidentally reject an exact-limit stream.

2. `AIMaximaContinuation::Reader::count` now calls `readCount("size")` with its 1,048,576 default, before checking the pre-existing 16,777,216 Maxima limit. A canonical 512x512 nine-neighbor table has 2,359,296 entries and is rejected. Narrow compatibility fix: pass the existing explicit Maxima container maximum to `readCount`, leaving the general stream default unchanged. This covers compact `legacyVector` loaders too. Other examined explicit Maxima count sites contain per-map arrays (at most 512² on supported maps) or entity collections, so no broader default expansion is warranted.

3. Reader caps also create a save-writer asymmetry: current writers accept snapshots up to uInt max and can replace a previous valid save with data the same build refuses to load. This also exists in the newly merged master. Consider rejecting snapshots larger than the expanded cap and counting compressed output against the compressed cap before rename; the level-zero compatibility path must obey the same acceptance rule. Failure should retain the previous file. This is a narrow persistence boundary consistency improvement, not an invitation to undo hardening.

The new map/resource, recursive-object and script checks do not require changes to compact field encoding or chunked ownership. Keep those master changes intact. Parent must validate the final rebase using large fixture loading and canonical large geometry, because prior benchmark binaries predate this hardening.

### Hardening integration resolution recheck

Re-read the final working-tree declarations and implementation. The separated 256 MiB compressed/2 GiB expanded limits, explicit Maxima 16,777,216 count maximum, compressed-output accounting, over-expanded writer rejection, and exact-limit one-byte inflate probe address the integration findings. The output counter never advances beyond its cap, so its subtraction is safe. Inflated size advances only after produced bytes pass the remaining-budget check. The exact-limit probe cannot expose payload and avoids allocating a spare block to finish CRC/trailer parsing. Literal callers' optional stricter expansion budget is clamped to the global cap; FileManager callers retain that global cap. Source declaration/definition signatures match and existing one-argument calls use the declared default.

The added harness cases exercise exact one-block budget success, exact capacity, one-byte-over rejection and zero-budget payload rejection. The newly merged untrusted-file sparse-input test remains valid. No new correctness problem found in these changes. One documentation clarification remains: the security guide's generic one-million AI/history collection statement should name the deliberate larger Maxima continuation exception for saved geometry. Parent informed; parent owns final compile/test execution.

## Final merge-readiness source audit

Rechecked `origin/master...5c4f47b01` after rebasing onto `62b1988da`, including security PRs #571 and #576. The #576 ManagementMisc/ManagementTracker, file-transfer, YOG assembly/distribution and untrusted-test changes remain untouched by this branch. #571 stream count validation, map/resource/body validation, recursion controls and script/network boundaries remain intact. The deliberate exceptions remain confined to the documented expanded gzip limit and Maxima's explicit existing continuation maximum.

The previously reviewed ownership fixes, expanded/compressed reader-writer consistency, CRC/trailing-data barrier, compact sentinel translation and legacy serialized widths are present. No new source-level merge blocker found. `git diff --check origin/master...HEAD` passed. Prior review conclusions remain applicable; parent owns final build/test checks and merge execution. Earlier native benchmark hashes remain historical evidence for the measured binaries, not measurements of this rebased head.
