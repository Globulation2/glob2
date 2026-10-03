# Maintainer-approved local merge gate

Source: aac206307, integrating master ade19a656. The native build started at
b62f4cdac; aac206307 changes only a duplicated workflow environment entry, so
compiler/source/dependency inputs are identical. The maintainer explicitly
authorized merging with relevant local checks passing despite hosted CI problems.
Hosted Windows/macOS and current hosted sanitizer results are not claimed.

Completed checks:
- Native game, unit/engine harnesses, skin preview, transport and online probes build.
- 646 native unit cases and 39 selected engine cases, no failures or skips.
- Live native API lifecycle probe passes against an isolated current-schema API.
- Three 180-frame classic/skinned comparisons: normal, overview (clamped to
  0.15625) and 5x zoom. Checksums match, drawing leaves simulation state unchanged.
  Warm geometry/raster work is zero; overview has no skin composites either.
  Screenshots inspected. Timings are smoke evidence on a heavily loaded shared
  machine, not matched performance measurements or new-base speedup claims.
- Threaded 600-tick balanced match, seed7, Maxima/Cortex/Warrush/Cabino, with replay.
- 288 build contracts (3 environment-dependent skips); 16 workflow policy checks.
- Translation checker: zero structural errors.
- Platform lint/typecheck; 374 tests pass,5 existing skips. The retained first
  full-check log includes a lazy-page one-second wait failure under extreme host
  load; the final complete test rerun passes after a ten-second wait. The newer
  upstream pages adjustment is separately validated:11 cases pass.
- Desktop/phone designer:6 cases. Accessibility:4 theme/device combinations of
  all14 pages. Production web application build passes.

Native and browser commands are retained alongside logs. Engine artifacts include
match records, verifier verdicts and per-tick checksums. Replays are losslessly
gzip-compressed for evidence storage; decompress before game playback. BMP captures
are losslessly converted to PNG. The earlier profiling/source and live authorized
match captures remain elsewhere on this evidence branch.

Browser rebuild and browser-gate results will be added when complete.
