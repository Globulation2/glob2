# Local validation evidence

Tested commit: `a0c69e144b5a505ae804422d680dd210b9a68e7d`. Fetched base: `3f67899b0899780efb4c309d111852b1e9f267ff`.

[Review and final verification](review-verification.md) records exact commands and coverage. [Original functional verification](verification.md) covers terrain and skin flows. Source content hashes were compared again after commit and match the tested implementation.

Environment: macOS arm64, Node 24.14.0, npm lockfile dependencies, PostgreSQL 16.15, isolated FFmpeg 4.4 / ffprobe 5.0.1, Chromium desktop/Pixel 7 emulation. Production Vite build; no native engine build flags. Final run: 292 unit tests; 35 browser tests plus one intentional desktop-only worker skip on phone; typecheck, ESLint/Prettier, 33-language inventory/launcher checks, production build, and diff check pass. Earlier terrain and skin functional evidence is retained because review changes preserve their domain behavior.

[Refreshed comparison index](review-comparison.html) links all six libraries in both themes at desktop/tablet/390px. GitHub renders linked PNGs directly; download the HTML and its screenshot directory to view the complete side-by-side gallery. [Screenshots](review-screenshots/) include empty/loading/error/filtered states and expanded Maps filters. [Browser evidence](review-browser-results/) includes player screenshots and the actual downloaded ZIP. [Skin evidence](skin-browser-results/) retains personal-collection screenshots.

Independent usability/accessibility and aesthetic reviews were completed, their feedback implemented, and the resulting design re-reviewed. Focus continuity, result announcements, control context, dropdown affordances, toolbar density, card hierarchy, and wrapped navigation were improved.

Scope and limits: UI-only; no API, schema, runtime dependency, or simulation changes. Chromium and emulation, not Firefox/Safari/physical devices. No hosted full CI or native simulation checks requested. Native compatibility checks are inapplicable to the changed UI boundary. This branch is evidence only and must not be merged into master.
