# Settings status pixel assertion recovery

Tested source 544ec37fd9e504c88c1127d6e5777c991c160d25, base298ee4f29a4d35f089b9375b4dcb62a1c2c1ebb9. Linux x86_64, Node24.19.0, pinned Playwright1.63.0, Chromium/Firefox/WebKit. Local browser release runtime6f90f649141d4686cc896304469463c5946302e6: production code unchanged since the startup repair; this candidate edits two JavaScript test files only. Exact invocation and isolated HTTP setup in run.py and after/playwright-result.json.

The existing visual predicate requires >50 pixels with every RGB channel below120. The failed quota and transaction-abort screenshots visibly contain 'Could not save', but their small support font has only18 such solid-dark pixels. Including antialiased ink below180 yields84 message pixels, while a bare paper region has0 under both thresholds. Original screenshots, hashes, exact clips and counts retained in pixel-counts.json.

Allow the shared helper's color threshold to be specified, preserving120 for all existing callers. Only the support-status checks use180; retain the >50 pixel floor and add a real blank-footer negative assertion. This still fails for absent text and checks actual rendered pixels. The persistence failure, retry, durable old copy, final saved values and reload assertions all remain.

All6 quota/aborted-transaction cases pass across the three browser engines in1.9minutes. No production or dependency changes. The previous native full2968-case pass and browser24-case determinism pass remain applicable to unchanged runtime code; full hosted latest-master CI remains pending. No broader runtime success is inferred from these visual fixture checks.
