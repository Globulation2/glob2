PR #1170 final integration verification

Source: 2ffb55941ff0c3226c07828ef33ed0a688613649
Base: 7556a9ecbc60b324a386e505533497974a811261

macOS arm64, Apple clang 21, full feature fast development engine and client.
82 focused integration cases passed, zero failures or skips. Golden update passed.
Client verification with compute participants 1 and 4: verified, no rejected orders,
843 actual checksum rows identical to each other and the committed trace.
Golden checksum trace unchanged from master; record regenerated for sim revision 54.
Independent conflict review preserved the original Nicowar fix and latest master snapshots/statistics.

See provenance.json, commands.txt, integration.xml and verification.tar.gz for details.
Initial packaging import-path failure and successful retry are both retained.
The final source was not retested on Linux, Windows or browsers, and no current
ordinary-release/performance/manual gameplay claim is made. Earlier Linux/macOS
evidence on the unchanged Nicowar implementation remains linked in the PR.
