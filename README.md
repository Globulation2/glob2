# Music QA integration inventory repair

Base9e903ff54a26b8bb26d67f5359fc4a9b43b8a02e; candidatea66104fa1a (see validation status for full revision).

After provisioning the existing pinned development requirements and verified acoustic/synth assets, the original unmodified music sandbox test passed. The original CPU render integration failed because its exact expected list omitted the documented transition check. The actual acoustic report passed QA and contained all12checks including score and transition. The test stops before the synth palette after this failure.

The focused candidate adds transition to the strict expected list and the matching pipeline fixture. It changes no runtime, audio, thresholds, waiver policy, assertion strength, timeout or sandbox restrictions. Both original palette renders and the existing pipeline tests are being re-run. No new music is being installed or shipped; these are the existing integration fixtures, not listening approval for a soundtrack change.

Baseline logs/JUnit and actual acoustic encoded audio/report are in baseline.tar.gz. Initial environment setup errors are preserved: a systemd service could not establish the user namespace; a scope provides the required real12GiBcgroup while preserving launch context. The namespace correctly omitted/etc, leaving distro BLAS/LAPACK alternatives symlinks unresolved. A private read-only library copy canonicalizes those2symlinks exactly as the deployment Dockerfile already does. Host libraries and the root/native/full-browser builds remain untouched; the original4GiBprivate scratch and unmodified inner sandbox remain enforced.
