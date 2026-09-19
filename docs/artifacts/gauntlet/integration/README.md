# Merge integration checks

Rebased onto Orchard Commons (`4a65126db`), preserving its ID 58 and assigning
Gauntlet ID 59. No terrain logic changed.

- The focused Gauntlet contracts, full defaults suite and five translation tests
  passed again; logs are retained here.
- The macOS golden update regenerated 456 rows. The [renumbering proof](renumbering-proof.txt)
  confirms that all eight Gauntlet outcomes/fingerprints match the provisional
  ID-58 build and that every upstream row, including Orchard Commons, is intact.
- The first CI run's Linux map-generator jobs passed all supported sweep cells
  but failed the required-baseline check for missing Gauntlet rows. Their observed
  [GCC 11 rows](linux22-golden.txt) and [GCC 13 rows](linux24-golden.txt) match exactly.
  Those eight Linux rows were then added to the table. They also match the seven
  successful macOS fingerprints and the expected undersized-map refusal.

These are generated-map comparisons, not cross-platform per-tick simulation checks.
CI reruns against the committed baselines are recorded on PR #347.
