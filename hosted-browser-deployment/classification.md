# Browser self-hosting deployment failure classification

[Job 124678310278](https://github.com/Globulation2/glob2/actions/runs/41244011872/job/124678310278), PR head `9b2162f3`.

The job fails before testing deployment: `actions/download-artifact` cannot find the `web-client` artifact. The actual “Test the web client package and deployment scripts” step was skipped. This is not the separate Caddy null-host matcher failure; no Caddy or deployment execution occurred here.

Upstream [WebAssembly job 124664682736](https://github.com/Globulation2/glob2/actions/runs/41244011872/job/124664682736) was cancelled while building the client after approximately 60 minutes (21:36:38–22:36:56 UTC). Its artifact-upload steps were skipped, explaining the missing prerequisite. This classifies the deployment failure; it does not claim successful browser build/runtime validation or prove why upstream compilation was cancelled.

`git diff --exit-code e67cba82e7 9b2162f3 -- .github/workflows/build.yml browser/package-static.py deploy/` is empty. No source changes, reruns or deployment actions were performed during classification.

Job metadata, upstream metadata, compact failure excerpts and the compressed sanitized job log are retained beside this report.
