# Corrected root-loader startup smoke

Six cases ran once on clean37, Chromium/Firefox/WebKit × requested serial/threaded. URLs use `/?threads=serial&renderer=webgl2` or `/?threads=threaded&renderer=webgl2`. Every case requires MainMenu and asserts both the observed runtime mode and explicit fallback reason. These checks do not create a match, toggle a torus, or repeat any failed render assertion.

Five cases passed. All six reached MainMenu. Chromium and Firefox selected threaded with a null fallback for threaded requests. All three serial requests selected serial with `serial requested`. WebKit's threaded request selected **serial**, reporting `worker shared memory unavailable`; its requested-mode assertion failed. This capability probe failure is evidence of fallback for this startup, not a cause for the separate Firefox sky-pixel failures.

The query is a request, not a guarantee of threaded execution. In particular, earlier WebKit root-entry torus results cannot be advertised as threaded coverage. The earlier original24 outcomes remain unchanged. Corrected future torus commands and mode-guarded copies live in ../corrected-reproduction and have not been executed.

See summary.json, the six browser/mode JSON records, retained failure trace, results.json and provenance.json. All source and recorded browser inputs remained unchanged. No retries were used; the server and test processes are stopped.
