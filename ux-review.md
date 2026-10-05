# UI/UX review and refinement

Reviewed the uncommitted player directory, versioned AI profiles, combined leaderboard and photo editing feature in two rounds. Read repository review requirements; inspected source, unit/browser tests and desktop/phone screenshots in light and dark themes.

## Round 1 findings and fixes

- Real Chromium Escape on the directory's native search input cleared the query and reopened results through onChange. Prevent the native Escape action so query is preserved and results collapse.
- ArrowUp with no active result chose the wrong item (and selected nothing with one result). Select the last item. Keyboard selection could move below the phone viewport without scrolling; scroll the active option into view.
- Cropper's keyboard target was an unnamed focusable element. Give the crop region an accessible name and instructions. Cancel previously left focus on BODY; close the modal before unmounting and restore focus to Upload photo. Freeze crop/zoom while saving.
- Directory errors offered no retry; added a retry action preserving search/filter state.
- Profile history refetched every preceding page on each Show more. Refactored to incremental cursor accumulation, retaining earlier matches during loading/failure and retrying only the failed page.
- Extracted shared player/AI URLs from the Players page into playerLinks.ts so other pages do not import a page for routing helpers. Use a shared version label including protocol number to distinguish versions with identical minor/hash labels.

Focused unit verification: 17 tests passed across players.test.tsx and pages.test.tsx. Web TypeScript and focused ESLint passed. First browser refinement run: 6/6 tests passed, desktop and phone Chromium, both themes, axe assertions. Exact command from platform: `PORT=4287 npx --yes --package=node@22 --call 'npm run build -w @glob2/web && npm run e2e -w @glob2/web -- players.spec.ts'`.

## Round 2 findings and verification

The original flat-color image fixture could not detect EXIF rotation or incorrect crop pixels. Replaced it with distinct quadrants, tested keyboard panning, fetched the saved avatar, and asserted orientation-specific output colors and 512x512 dimensions. Added save failure/retry checks verifying the crop remains editable and cancel stays enabled; cancel sends zero upload requests. Added real-browser Escape, ArrowUp and active-option viewport checks. History regression verifies request cursors are initial/older/older through failure and retry, with earlier results retained.

Second browser round: 6/6 passed in 38.9s. Command from platform: `PORT=4287 npx --yes --package=node@22 --call 'npm run e2e -w @glob2/web -- players.spec.ts'`. Reviewed refreshed dark-phone crop and versioned profile screenshots: controls fit, focus ring is visible, and graphs/cards remain legible. No further substantive UI findings.

## Evidence and limits

- `ux-round1-browser.log`, `ux-round2-browser.log` contain successful browser output.
- Screenshots: `artifacts/web-app/players/{desktop,phone}-{light,dark}-{directory,ai-profile,crop}.png`.
- Chromium desktop and Pixel 7 emulation only; no real-device, Firefox, WebKit or assistive-technology session. Axe and keyboard checks supplement but do not replace screen-reader testing.
- Server on isolated port 4287 was stopped after each run; unrelated existing port 4280 preview was untouched.
- Broader final integration checks and native compatibility coverage are owned by the parent agent and recorded separately.
