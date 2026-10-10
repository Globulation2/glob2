# Platform development

Set up the TypeScript workspace, run checks and maintain translated interfaces.

## Working on the platform

The workspace needs Node 22.18 or newer (TypeScript runs directly through Node's
type stripping, so there is no build step except for the web app).
`npm run typecheck` checks server code, the web app, and browser end-to-end tests
in separate TypeScript projects; the latter includes DOM types for code evaluated
in the browser without adding browser globals to server checks.

```sh
cd platform
npm ci
docker run -d --name glob2-pg -p 127.0.0.1:55432:5432 \
  -e POSTGRES_USER=glob2 -e POSTGRES_PASSWORD=glob2 postgres:16
npm run check        # lint, format check, type check, translation contracts, tests
npm run fixtures     # regenerate protocol fixtures after schema changes
DATABASE_URL=postgres://glob2:glob2@127.0.0.1:55432/glob2 npm run migrate -- latest
DATABASE_URL=… node apps/api/src/main.ts
```

Tests create and drop their own databases on the server named by
`TEST_DATABASE_URL` (default `postgres://glob2:glob2@127.0.0.1:55432/postgres`).
Requested hosted platform verification runs against a Postgres service;
ordinary PR checks are lightweight and local verification is the standard path.
See [contributor verification](../../AGENTS.md#validation-and-ci-feedback).
Generated protocol fixtures also select native compatibility checks; these include the C++ contract tests. The web app's Playwright suites
(`apps/web/e2e`: page smoke tests and axe accessibility checks) run locally with
`npm run build -w @glob2/web && npm run e2e -w @glob2/web` against a seeded API
on the test Postgres. The whole deployed stack, including a rated match, has its
own one-command test; see
[End-to-end test of the stack](../hosting/operations.md#end-to-end-test-of-the-stack).


## Online interface languages

The online interface supports the same language inventory as `data/texts.list.txt`.
`platform/packages/i18n` owns the canonical English messages, translated JSON
catalogs, browser locale mappings, interpolation, plurals and formatters. Game
catalog aliases are mapped explicitly: browser language tags must never interpret
`br` as Brazilian Portuguese or `si` as Slovenian. Arabic and Persian use RTL.

The web application, server-rendered sign-in/invitation pages and browser launcher
share a `glob2_locale` preference cookie. An explicit selection takes priority over
browser preferences; unsupported preferences fall back to English. The browser
game retains its saved language, using the online selection only as an initial
preference when no game language has been saved. Switching the web language keeps
open editors and unsaved drafts mounted.

English source messages are catalog keys. Use named parameters for complete
sentences and named React slots for links or emphasis; keep IDs, URLs, source code
and user-authored content outside translation. Count-dependent messages use plural
variants selected by `Intl.PluralRules`. API errors retain their English `message`
and machine `code`, with optional `messageKey` and `messageParams` for translated
presentation. Unknown diagnostics use a generic localized explanation.
System-generated studio artifact and validation captions are normalized in
`platform/apps/web/src/messages.ts`; changing a producer caption requires updating
its presentation mapping and all catalogs. Preserve authored titles and diagnostic
details.

Update every catalog when adding a message. From `platform/`, run
`npm run i18n:check` to validate language coverage, message keys, parameters,
plural forms, literal API identifiers and source coverage. `shared-values.json`
records reviewed messages intentionally identical to English, such as brands,
licences and symbols. The source check rejects untranslated JSX copy and
accessibility labels. Run `npm run i18n:browser` to regenerate the standalone launcher
runtime and development catalog copies. Browser builds install the canonical
catalogs directly. Generated launcher runtime changes belong with their source
changes. AI drafts require independent review of meaning and terminology;
structural validation alone does not establish translation quality.

[Multiplayer index](README.md) · [Documentation index](../README.md).

## Shared web design system

The online application and sign-in/invite pages consume
[`@glob2/design-system`](https://github.com/Globulation2/glob2-design-system).
Reusable tokens, CSS, icons, fonts, brand imagery, accessible primitives and theme
persistence live there. Application layouts, translations and workflows remain here.
The native game and `/play/` runtime retain their own styling and assets.

After `npm ci` in `platform/`, run `npm run design:sync`. Vite also bootstraps this
at the start of development/builds. Each invocation resolves design-system `main`
once and installs that exact commit in isolation, preserving other locked dependencies.
For coordinated verification, set `GLOB2_DESIGN_SYSTEM_SHA` to a full published commit
SHA before all checks and builds. The bootstrap fails on resolution/install errors;
`.cache/design-system.json` records the revision and installed-file fingerprint,
and the web build serves `/design-system.json`. CI resolves once and exports the SHA
to later steps. No Git-install lifecycle scripts are needed.

Import shared foundations before local CSS. Component styles opt in with `.g2-ui`;
local CSS owns only product layout and domain-specific presentations. Use the app's
thin translation wrappers for theme labels, loading text and wordmarks. For new
components or tokens, change the shared repository and test its candidate SHA here.
See its examples and downstream guide for HTML, Astro and React integrations.

The external `/theme.js` bootstrap runs before paint. HTTPS production sites share
`glob2-theme` on `glob2online.com` (one year, Secure, SameSite=Lax, Path=/), including
an explicit `system` choice. It migrates the previous localStorage key after successful
persistence. Localhost and unrelated previews use origin-local storage. Returning to
a tab or restoring a page re-reads the preference; blocked persistence preserves a
usable in-memory choice. Server pages use the same package bootstrap and rebased CSS
under `/signin/assets/`, allowed by their same-origin CSP.
