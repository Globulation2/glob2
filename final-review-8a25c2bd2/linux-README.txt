Linux evidence for 8a25c2bd2440e99246171967a31c68dc15917f95
Verify files with: shasum -a 256 -c linux-SHA256SUMS
Each tar archive includes its entire final lane, retained original master124 proof, driver log and Linux peer comparisons.
For split archives concatenate .part-001 and .part-002 in order; verify archiveSha256 in linux-summary.json, then extract with tar -xzf.
All published files are below100MB; parts are at most80MiB. Every retained archive member was verified against its original file.
All three Linux lanes match macOS across60 shared artifacts. Fresh and resumed legacy AI runs match their respective original upstream baselines; inherited Castor resumes differ from uninterrupted games.
