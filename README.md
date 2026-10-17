# PR 1197 primary-candidate gradient preparation

Production change: [c7f1cd299](https://github.com/Globulation2/glob2/commit/c7f1cd2996a13ac59d56926755c182008b67d736), in [PR 1197](https://github.com/Globulation2/glob2/pull/1197).

- [Production qualification](qualification/README.md): exact commit, build flags, focused test coverage, saves, replays, checksums and limitations. The wider unit run is **not green**, and Linux comparison was interrupted by infrastructure failure. The PR remains draft.
- [384-run prototype benchmark](benchmark/report.md): 32 paired seeds per fixed map, one/four compute participants, full-shortlist versus primary-only warming. Raw runs, source snapshots, commands and uncertainty intervals are included.

The benchmark used four scheduled refresh admissions **per team** in every variant. That scheduling change remains separate in PR 1220. Its optimized prototype timings are not measurements of the production binary qualified here.

Each folder has a SHA-256 manifest. Generated evidence is intentionally kept on this separate evidence branch, outside the production source/documentation tree.
