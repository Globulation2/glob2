Tested source b7df2e288 against master f66fe1866a505bfad4ac698edb56cbca63690db3.
Ubuntu 26.04 x86_64; Node 22.22.1; Playwright 1.63.0 with bundled Chromium, Firefox and WebKit.
Runtime: unmodified web-client artifact from hosted run 37184200265 (f66fe1866).

Commands from repository root:
python3 browser/serve.py 8775 --bind 127.0.0.1 --directory artifacts/ci-repair/f66-web-client
Then from browser/:
GLOB2_TEST_URL=http://127.0.0.1:8775 npx playwright test tests/editor-storage.spec.js --project=chromium --project=firefox --project=webkit

Result: 6 passed (3.6m), no retries. Both faults check exact export, durable retry and reload.
Original failure: run 37184200265, job 111384422908. Retained screenshot shows Saving and disabled OK at null-file assertion. Global persistence status also reflects automatic writes. Corrected fixture waits for the dialog's Export control after this save finishes.
Only fixture changes: production source, assertions, timeouts and CI coverage unchanged. Native checks not repeated for JavaScript-only fixture change. Overall full latest-master verification remains ongoing.
