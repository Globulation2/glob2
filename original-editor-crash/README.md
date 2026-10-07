Original Chromium editor-crash failure artifact from run37570683268, artifact11461861889. Complete raw ZIP is split without recompression to keep each Git file below100MB.

Reassemble: `cat browser-failure-traces-1.zip.part* > browser-failure-traces-1.zip`. Verify SHA256 `da8a7e5147fbbe19e1f17d33cecde57711bf5aac546b763160ff435bff009e4c` and length163618770bytes before extraction. Individual part hashes are in [download manifest](../platform/final-platform-downloads.json). The original crash remains unexplained despite a passing exact-artifact rerun.
