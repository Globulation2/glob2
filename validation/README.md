# Final production validation

Validated production revision dd38ad2902d3860669c42992a994bd288e96719b, based on9ece139a26. The frozen tournament snapshot elsewhere in this evidence branch is historical benchmark provenance, not this production patch.

All364native cases,77Python Maxima cases and25main-CLI scenarios pass on Linux/GCC15.2 release. The retained v115 save matches all512committed per-tick records in one- andfour-participant execution; midpoint save/reload independently matches all256remaining records. Save193/NET75 are unchanged; SIM121 matches the regenerated match record. The active old-save expected trace changes260ticks starting30252 because the intended cap changes AI behavior; the checker validated reload equality before writing the golden.

See commands.json, RESULTS.json, compressed logs, JUnit, saved continuation/checksum artifacts and final-independent-review.md. Hosted cross-platform/full CI was requested and is reported separately on PR1358; Linux validation alone is not cross-platform certification. Existing development O0 tests hit two120s heavy-food timeouts; unchanged tests pass in release with no timeout change. A stale experimental strategy-dump helper was rebuilt, after which all77Python cases pass.

Normal development dependencies remain in the external user cache. No new runtime dependency, save state, strategy parameter or experiment runner is part of the production PR.

## Browser reference refresh and final source mapping

Final source revision4563ce85cea1f39ca1d9ba776057427eb2cc602b adds exactlythree JSONmetadata field changes to the two Studio references: both simulation revisions120→121 and the generator simulation-version identity. It changes no runtime code. The364native,77Python,25CLI and save qualification above remain honestly attributed to dd38ad2902; they were not rerun after this metadata-only commit. The qualified native binaries retain identical hashes.

`browser-reference/` retains fresh native-generated pinned checksum traces (lossless gzip), preview report/fingerprint, exact commands,174passing Node contracts, one passing generator Studio harness case, before/after fixture hashes and independent review. The edited fixture bytes tested beforecommit exactly match4563. `revision-mapping.json` records the boundary. Generator generation and initial-save loading match every checksum byte. No local serial/threaded browser runtime matrix is claimed by this refresh. Raw profiles, saves and redundant supplemental probe traces were omitted; the compact evidence explains their comparison results.

## Hosted audit

`hosted-ci/hosted-ci-audit.md` and compressed excerpts verify the inspected Windows/macOS native successes at the precise older head/merge, eight failure causes and three cancelled/unavailable checks, including browser/native simulation equivalence. Browser timeouts were not reproduced on baseline and are not dismissed as proven pre-existing. The subsequent full CI run is pending/unqualified here; this evidence does not call full CI green.
