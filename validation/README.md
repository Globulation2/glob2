# Final production validation

Validated production revision dd38ad2902d3860669c42992a994bd288e96719b, based on9ece139a26. The frozen tournament snapshot elsewhere in this evidence branch is historical benchmark provenance, not this production patch.

All364native cases,77Python Maxima cases and25main-CLI scenarios pass on Linux/GCC15.2 release. The retained v115 save matches all512committed per-tick records in one- andfour-participant execution; midpoint save/reload independently matches all256remaining records. Save193/NET75 are unchanged; SIM121 matches the regenerated match record. The active old-save expected trace changes260ticks starting30252 because the intended cap changes AI behavior; the checker validated reload equality before writing the golden.

See commands.json, RESULTS.json, compressed logs, JUnit, saved continuation/checksum artifacts and final-independent-review.md. Hosted cross-platform/full CI was requested and is reported separately on PR1358; Linux validation alone is not cross-platform certification. Existing development O0 tests hit two120s heavy-food timeouts; unchanged tests pass in release with no timeout change. A stale experimental strategy-dump helper was rebuilt, after which all77Python cases pass.

Normal development dependencies remain in the external user cache. No new runtime dependency, save state, strategy parameter or experiment runner is part of the production PR.
