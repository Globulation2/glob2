Final Bulgarian online verification passed for product commit b07193e1deafa102cb694f9cca52c529a904e8dd. The build/captures ran after later documentation/native-tooling commits, with no difference from that commit in platform sources, catalogs, browser assets or fonts. provenance.json and bundle-manifest.json bind the source and built assets.

Environment: Linux x64, Node v24.22.1, Playwright 1.63.0, headless Chromium 153.0.8010.12. Desktop viewport 1280×900; phone emulation 390×844. The layout case explicitly checks 390×900 and 1280×900. Six standalone review PNGs use 390×844 and 1280×900. Explicit Bulgarian locale cookies and bg-BG browser locale were used.

Validation:

- Final Vite build passed in 46.75 seconds. Focused ESLint, Prettier and i18n source/inventory checks passed; i18n covers 37 languages × 2843 messages.
- Browser suite: 11 passed, 1 intentional duplicate-phone layout skip, 3.0 minutes, 1 worker. tests.json and html-report contain the exact assertions and attachments.
- Homepage and server sign-in load the bundled Bulgarian font and retain the explicit language choice through reload.
- Twenty route/viewport layout checks cover player/map/building details, account, map/AI/generator/music upload forms, terrain-set authoring and the building editor. Controls stay inside the viewport; document overflow stays at zero. Text-range checks also prove long technical building identifiers do not cover the Remove controls.
- All seven AI filter/publish labels and five populated card/detail tag labels are Bulgarian. Selecting the translated Rush label still sends canonical tags=Rush. Map option labels are translated while public/unlisted/private values remain unchanged. Music metadata uses translated Title, Artist, Description and attribution-specific Credits & sources.
- Newly created set and building drafts receive Bulgarian initial names. An explicit author-supplied building name survives save/reload and language switches to English and back to Bulgarian.

API coverage: authentication/session fixtures, sign-in HTML, history/detail routes, upload/authoring forms, building creation and saving use a real isolated API and disposable PostgreSQL database. AI populated list and detail responses use schema-shaped e2e fixture data; filter request values and rendered card/detail labels are asserted. No real provider request, payment, email or external installed-app launch occurred.

The six final PNGs and screenshots/report.json were inspected directly. Bulgarian glyphs, initial names, music attribution fields, long actions and wrapped technical identifiers are readable. Browser-owned Choose File/No file chosen captions retain the headless browser installation's UI language; these captions are outside the app catalog. Safari, Firefox, physical mobile devices, full provider-backed studios, payment flows and WebAssembly multiplayer rooms are outside this final suite.

TypeScript provenance: the agent's passing focused web/e2e chain spanned integration and is retained only as provisional evidence. Exact final shared/API/web/e2e coverage is consolidated separately in artifacts/bulgarian/typecheck-coverage-final.json; the fresh final e2e log is artifacts/bulgarian/generated-draft-names-e2e-typecheck.log. Do not substitute the earlier mixed-revision chain for those checks.

Earlier provisional browser evidence remains in ../provisional: two map assertions used a label locator that included option text and resolved no controls. The corrected accessible-role locator passes in both final viewport projects; there was no untranslated map-option product defect.

Cleanup completed: the private API on 42769 was stopped and only the task-owned glob2-bulgarian-final-online-qa PostgreSQL container on 42770 was removed. Other jobs, services and databases were not stopped or reconfigured.
