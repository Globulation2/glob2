# Browser introduction transition evidence

Base and compiled release Wasm: `70319a0da004a6473ef974ceb2415a9ccf072488`.
Test helper revision: `5c1025180ac6be6a76b157e1d48ffc842b46d790`.

The actual hosted threaded colony-skin failure clicked intro/menu repeatedly while its old introduction diagnostics remained published. Its final screenshot shows the main menu, while the helper failed waiting for the retired introduction control. The original local threaded case reproduced this failure; the one-choice helper passed the complete original threaded skin case, including its original context restoration and mesh assertions. Original timeouts and case inventory remain unchanged.

Serial startup still fails both the original helper and candidates. Campaign persistence retry passed once on the baseline and three times with each candidate; its actual hosted failure is retained, not declared fixed. A separate full default local run reported a renderer presentation-owner lookup failure; the isolated original case passed. No simulation or runtime code changes are included in this repair.

`diagnostics.tar.gz` contains actual hosted trace event streams and final screenshots, compact decoded snapshots, local JUnit/logs/status, and negative experiments. Repeated hosted screencast frame bitmaps are omitted; complete original archives remain in GitHub Actions artifacts of run 41604923022 and are preserved locally.

Artifact 13222848640: 359904416 bytes, SHA256 `76dfa4856eec64d8af2a1ac1ee564936ca991172881474b170716d9d4b7f26ac`.
Artifact 13224006484: 186754450 bytes, SHA256 `ea925ecfa3a762cb32c63735efa0badfed500368c38a353fbd6cf570b5414527`.

Local focused probes restrict Chromium to two physical cores, using the current compiled runtime and default Chromium backend. These are functional observations under local resource contention, not performance acceptance or speed claims. The direct mouse variant did not solve serial startup and was reverted. Current GL timing and CPU profiles do not establish a production performance fix. All platform compatibility and full latest-master CI requirements remain in force.

Final focused qualification: both repeated original full threaded colony-skin cases passed (180.2s and84.5s runner wall time), plus the original menu/resize case in Chromium, headed Firefox and WebKit (17.5s,32.9s,26.0s). Five cases, zero failures/skips. The first repeat overlapped the read-only serial CPU profile on the same two physical cores; no speed claim is made. `qualification.tar.gz` retains exact commands, revision/configuration, JUnit and logs. Source helper remains5c1025180ac6be6a76b157e1d48ffc842b46d790 and master base remains70319a0da004a6473ef974ceb2415a9ccf072488.
