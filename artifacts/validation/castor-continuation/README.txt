Castor continuation review evidence

Fix: PR #512, ac574fca8 and 73833fbc5. Original Lifecycle.cpp: master 98301f7cb. macOS ARM64, Apple Clang 21, -O0 native coverage instrumentation.

original-continuation.log reproduces the original timer-only save failure at checkpoint 1, resumed poll 48. The fixed-restored logs cover all four new Castor cases.

The two gzipped traces contain 512 complete ticks of order bytes, simulation/entity checksum fields and complete RNG state. The current harness and headers are held constant; only Castor Lifecycle.cpp is replaced by the original master file for the baseline. The harness excludes MapHeader format metadata. comparison.json records the byte equality and SHA-256 hashes. The comparison probe records the exact commands and temporary trace instrumentation; adjust its two local directory paths to reproduce elsewhere.

Cross-platform checksum equivalence is not established by this same-platform comparison. Older Castor AI formats 1 and 2 remain readable with historical restart behavior.

This branch contains temporary validation artifacts for review and is not intended to merge into master.
