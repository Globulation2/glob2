# Menus and dialogs: the declarative UI framework

Every frontend menu, in-game dialog and editor dialog is one element tree that
the screen rebuilds from its model. The framework in `libgag/include/ui/`
(namespace `GAGGUI::ui`) owns layout, phone/tablet/desktop adaptation, live
relayout on resize and rotation, input, focus, scrolling, popups and theming.
The game-side bindings in `src/ui/FrontendUI.h` (namespace `Glob2UI`) add the
two themes, translation and shared page builders. Screens contain a model and a
`build()` function, nothing else.

The shared screen stack separates updates from painting. `frame(tick, events,
false)` dispatches input and advances screen work without drawing;
`draw()` paints when the graphics context's render ceiling permits it. Native
hosts normally use the combined `frame()` call. Browser hosts call `Loop::draw()`
on animation frames separately from timer-driven updates; render deadlines never
shorten browser update timers. Screen logic must not
require a paint to advance loading, networking, input handling or persistence.
DOM text-field changes and actions are delivered in `frame()` before transitions
and SDL events; `draw()` must not dispatch input or admit screen transitions.
The **Target render FPS** preference applies to screen and dialog drawing as well
as matches and the editor; see [render pacing](../architecture/rendering.md#target-render-fps).

## Rules

1. **Build, do not lay out.** A screen implements
   `Element build(const Presentation &p)` and returns a tree of containers and
   controls from its current model. Never compute pixel positions in a screen;
   express structure (column, row, wrap, footer, scroll) and let the host
   measure and arrange it for the current viewport.
2. **Change the model, then `invalidate()`.** Callbacks mutate the model and
   call `invalidate()`; the host rebuilds on the next frame. Any method that
   changes what `build()` would return must invalidate, including ones reached
   from timers, network listeners and harness entry points. The host also
   rebuilds on its own after resize, rotation, inset, keyboard and scale changes.
3. **Give every interactive element a stable string key.** Keys retain focus,
   scroll offset, pressed state, popup state and text drafts across rebuilds,
   and harnesses drive screens through them (`host().bounds("start")`). Use
   slash-separated paths for repeated controls (`colony/2/team`,
   `map/list/1`). Duplicate keys are an error the presentation harness reports.
4. **One tree, adapted by the presentation.** Read `p.compact()`,
   `p.phone()`, `p.landscape()`, `p.shortLandscape()` and `p.touch` to choose
   structure inside `build()`, or use `adaptive()` to choose by the width the
   parent actually offers. Do not keep separate desktop and phone build paths
   for one screen; both must share the model and the keys.
5. **Sizes and text are points, resolved through `p.pt()`.** Theme metrics
   (control height, gap, padding, minimum touch target, dialog width) are host
   points; `p.pt(points)` converts to logical pixels for the current scale. Do
   not use raw pixel constants. The framework sizes text from the player's
   text size (in points on touch hosts), so a screen never scales fonts itself
   (see [Text size](#text-size)).
   Use `p.textPt()` for widths that must grow with text, such as touch action
   grids. Settings places its save-status note above the actions when the
   available width cannot hold both comfortably at the chosen text size.
6. **Text wraps or ellipsizes; it is never clipped silently.** Use
   `paragraph()` for wrapping text and `label()` for single lines that
   ellipsize. Controls shrink or wrap their labels within the framework's
   minimum sizes.
7. **Actions go in a `footer()`.** The footer pins the action row to the
   bottom while it fits and folds it into the scrolling body when the viewport
   is too short (keyboard up, short landscape phone). The Escape route is the
   last action and carries `SDLK_ESCAPE`; the primary action carries
   `SDLK_RETURN` where a form has one.
8. **Screens never see coordinates.** Only the host converts SDL events. Gameplay
   forwards already translated events to `UIDialog::eventLogical`; standalone
   dialog callers use `event` for raw window coordinates. A
   screen that must react to a raw event overrides `interceptEvent()` (consume
   before the host) or `onEvent()` (after), and does so for keys, not
   positions. Custom painting goes through `canvas()`, which hands the painter
   the arranged rectangle.
   Batch native events with `GAGCore::EventQueue`, which owns SDL3 text-input
   and composition strings. Use it also when deferring input to a gameplay
   tick; SDL's temporary text pointers must not survive another event pump.
9. **Dialogs are hosted, never looped.** A `UIDialog` is driven by its owner
   (`GameGUI`, `MapEdit`, `EndGameScreen`, a parent screen) through
   `event()`, `update()`, `draw()` and `finished()/result()`.
   Mouse events passed to `event()` already use the owner's logical
   coordinates; its input boundary converts SDL window coordinates once.
   There is no nested event loop and no captured background; the scrim and panel are
   painted onto the owner's surface each frame.
10. **Keep harness entry points semantic.** Expose what a test needs as a
    method on the screen (`selectTab`, `setServer`, `confirm`) rather than
    giving tests widget internals. Screens may name harness structs as
    friends for state inspection.
11. **Keep the established look.** The builders above already reproduce the
    former desktop and phone layouts. Before changing how a screen looks,
    compare it against the previous release with the gallery
    (`tools/mobile_gallery/capture.py`) at a desktop and a phone size; the
    framework is a means of keeping screens responsive, not a redesign.
12. **Register new text keys** in `data/texts.keys.txt` with English fallback
    text in `data/texts.en.txt`; the capture runs fail on missing keys.
    Leave untranslated catalog values blank so the runtime uses English, and mark
    those catalogs with `*` in `data/texts.incomplete.txt`. When English fallback
    is accepted for new controls, list only those keys in `data/texts.pending.txt`;
    strict validation still rejects other untranslated text and all structural
    errors. Remove pending keys once every catalog has a translation.

## Translation coverage checks

Run `python3 data/check_translations.py --strict` and
`python3 test/test_translations.py` when changing user-visible text.
Use `python3 data/check_translations.py --json` for per-language missing,
untranslated, shared-English and placeholder findings, plus the source coverage
inventory. The checker reads the catalogs without rewriting them.

Native source checks join adjacent C++ literals, skip comments, and include keys
stored in arrays or passed through UI wrappers and conditional expressions.
Registered implicit conventions cover settings labels/help (including category
names and scroll sliders), `musicText`, `customAIText`, `libraryText`, statistics metrics,
experiments, shipped theme IDs and ruleset names/descriptions. Add a scanner rule
and a regression fixture when introducing a new wrapper or computed key family.
Direct prose passed to common frontend constructors, action tables, choice lists
and map-overlay text is also rejected. A successful
catalog check cannot detect every string assembled through variables or new helpers.

The JSON `coverage` field records checked families and remaining review surfaces.
Current tutorial text is authored as registered keys in the JavaScript lesson
catalog; localization audits report its per-language gaps. Old SGSL saves use
their own embedded language blocks through the ordinary scenario-message overlay
and history. No source fingerprinting or retired-tutorial text alias table is
maintained. Loading a save preserves its script bytes, execution offsets and
progress; old displayed messages may retain their original wording or language
fallback until the script emits another message.

Campaign mission titles are localized for display while selection still uses
original mission IDs. Unrecognized custom text is displayed as authored. Legacy desktop tutorial and
history messages use the shared UTF-8 wrapper, including unspaced CJK text and
explicit line breaks.

Player-name entry retains the existing 32-byte limit. Native text fields reject
edits that exceed the cap; the browser bridge bounds text at complete graphemes
and rejects overflow when grapheme segmentation is unavailable.
`Settings::setUsername` also retains the prior value when command-line or other
direct callers exceed the cap. Existing loaded and received names keep their
current formats and byte limits; language support does not enlarge serialized
player-name fields.

The JSON tutorial inventory distinguishes legacy language blocks from current
`display_catalog_gaps`. Generator/building/resource names and keyboard names
built from runtime data and unregistered wrappers still need contextual review.
Run `npm run i18n:check` from `platform/` for online catalogs and browser launcher
bindings. It also requires catalog keys for fixed UI metadata: AI tags and compatibility
captions, connection-quality terms, route/admin/map labels, match verdicts, music
moods/check guidance and skin model/material/shape/pose captions. Translate their displayed labels while preserving canonical protocol
values in filters and requests. Render representative screens in each language to
check font coverage, text fitting and layout. The legacy Arabic/Persian route in
`TrueTypeFont` shapes text into visual order before rasterization and uses an
explicit left-to-right SDL_ttf direction to prevent a second reordering pass.
Complex script fonts retain their shaping tables and use logical directional
runs with HarfBuzz instead. Urdu, Shahmukhi Punjabi, Pashto and Sindhi select that
route by language code; changing languages invalidates affected font caches.
Do not send presentation-form Arabic through the logical shaping route. The
FontKerning suite checks raster sizes, reloads and language switching.

Map visible captions from stable IDs to translated source strings instead of
capitalizing the IDs. This includes skin models, materials, shapes, views and
poses, music metadata fields, and chart axes and legends. Preserve the IDs
themselves: the swarm's empty pose
ID is valid and needs a localized static-pose caption.

Translate display text before passing it into shared component props, including
composer pricing, edit targets and disabled reasons. The extractor does not
discover an untranslated string merely because a component renders that prop.
Resolve example labels and their inserted prompts at render time so language
switching updates both; retain stable identifiers separately from those labels.
Keep protocol registries and stored custom names intact; localize fixed registry
labels in the UI and translate system defaults when creating a new document.

Legacy scenario-message panels use the active font’s measured line height. Desktop messages
retain a 20-pixel minimum advance; touch instructions use the shared cluster-aware
wrapper and a 24-point minimum advance. The touch cache includes language, font,
width and text-size inputs. Drawing, scroll limits and hit areas share the same
measured footer and collapsed-label bounds. Test both expanded instructions and
the collapsed control when changing fonts or text-size behavior. The collapsed
mobile tutorial uses cluster-safe title ellipsis; expanding it restores the full
wrapped title. Mobile statistics, the Tools strip and map peek temporarily own
foreground painting and input. They cancel tutorial pointer and keyboard focus
without changing its lesson or collapsed state; statistics paint above the minimap.
Collapsed browser account actions retain their complete translated accessible
names and show an icon in a 44-pixel target.

Online music controls use `musicMoodLabel` for visible names and accessibility labels.
The Bulgarian construction phase reuses `Build` (Изграждане), while `Building`
continues to name a structure elsewhere. Keep mood identities and upload path
segments canonical; translate the displayed label after selecting the wire value.
Music notices retain original error objects and mark generated success explicitly.
Bulgarian error captions translate explicit API message keys or show the reviewed
generic failure, preserving raw download messages for other locales.
Set property controls reuse reviewed captions for known properties. Other schema
fields and material names appear as their exact identifiers in code styling;
JSON keys and enum values remain canonical.

### Native API error presentation

`Online::ApiError` retains raw code, message and details for diagnostics and
control flow, and decodes optional source `messageKey` and scalar
`messageParams` separately. At a player-facing boundary use
`Glob2UI::apiErrorText` before converting a typed error into a notice or exception.
The wrapper reads the current StringTable language code. Do not rewrite
PlatformClient diagnostics or translate script/JSON exception text with it.

Local library and storage failures use `Glob2UI::exceptionText` at the UI catch
boundary. Bulgarian shows an existing reviewed summary and logs the original
diagnostic; other locales retain their previous message. Throw UI-only
`PresentedError` for deliberately localized API or native messages so a catch
preserves their specific wording. Its optional original diagnostic preserves
logging and prior non-Bulgarian behavior. Repeated display paths use a
per-context `DiagnosticText` cache: exact existing native keys retain their
wording, unknown Bulgarian diagnostics use the reviewed summary, and unchanged
failures do not log again on every paint. Keep raw validation and readiness data
separate from displayed summaries. Hive reports retain their public raw string
history; only explicit local producer metadata permits application acknowledgments
to be translated. Remote reports and user-authored order text are never classified
by matching their contents. Keep `PresentedError` out of models,
protocols, storage and CLI code, and retain cleanup and rollback handling.

Bulgarian initially uses the already reviewed English/Bulgarian online catalogs.
`tools/localization/build_online_error_catalog.py` generates the embedded resource
with exact input SHA-256 identities. The shared `scons/online_error_catalog.py`
rule is called independently by native, serial/threaded browser, Android and iOS
builders, including test and unit-production consumers. It generates under each
builder’s existing `include/generated` directory, with explicit consumer
prerequisites and no global include-path/compile-signature change. Its
ASCII-escaped JSON is split into portable string chunks. On the first Bulgarian error it joins and parses
the resource once into a process-static lookup; other locales return before that
work. Startup/first-error cost on target devices must be profiled separately.
Plural objects are excluded because ErrorBody has no plural selector. Add other
locales only with reviewed catalog and size coverage.

The pure `Online::presentApiError` handles known source keys and substitutes
scalar parameters once as literal text. Legacy raw messages match only an exact
English source literal. Unknown or malformed messages use the existing reviewed
“Something went wrong. Please try again.” summary in Bulgarian; other locales
retain their original message. `onlineProblemText` applies that safe boundary to
plain transport diagnostics while keeping raw model/log details intact. Existing
specialized cooldown, version and conflict branches remain authoritative.
Generator tests live in `test/test_online_error_catalog.py`; decoder/presentation
cases live in `src/online/ApiErrorTextTest.cpp` and the ordinary protocol suite.

### Preparing independently reviewed locales

Use `python3 tools/localization/translation_program.py prepare` to freeze the
current English game/online sources and their contracts under
`artifacts/translation-program/`. The kit records the source revision and hashes,
groups messages into coherent domain batches, and initializes review records with
all gates pending. Use a new `--output` directory after changing frozen source
text; preparing an identical kit preserves existing work. Generated inventories,
drafts, reviews and screenshots are local evidence, not committed documentation.
Use `prepare --locales hi ur sw --output artifacts/localization-release` to limit
a kit to a selected release scope. A changed source entry requires fresh review
of that entry; carry older reviews forward only with explicit proof that their
source and translated values are unchanged. Preserve the original evidence and
describe that hash-based carry-forward rather than claiming a fresh reading.

Run `python3 tools/localization/translation_program.py draft --locale hi` to create
an unregistered draft with blank translations and the locale's required online
plural forms. Drafts never overwrite existing work. `check-draft --locale hi`
checks draft keys and tokens and reports untranslated entries and English matches
for review; it does not replace the official checks above. Keep unfinished
catalogs out of the selectable language inventory.

Audit platform startup text outside these two catalogs as well. Android shows
`preparing_game` from `mobile/android/app/src/main/res/values/strings.xml` while
staging assets, before the native catalog can load. Add the reviewed counterpart
in the locale's Android resource directory (for example `values-mr/strings.xml` or `values-bg/strings.xml`),
retain separate source/value hashes and independent review records, and check
the actual first-run overlay in the packaged application. The catalog inventory
checks alone do not establish that this startup text is translated.

Apple's local-network permission explanations also live outside the engine
catalog: `darwin/Info.plist` and `mobile/ios/Info.plist.in` have distinct source
purposes. Review their localized `InfoPlist.strings` separately. macOS bundles
copy the declared `.lproj` directories into `Contents/Resources`; iOS CMake
resources use `Resources/<language>.lproj` (CMake removes the `Resources` prefix
for the flat iOS bundle). Keep `en.lproj` and `CFBundleDevelopmentRegion=en` as
the fallback. Packaging contract tests verify copied files and resource paths;
they do not establish Apple permission-dialog behavior on a device.

For a Tamil plain-text Google Cloud NMT comparison draft, authenticate with `gcloud` and use the
budget-limited development helper on an existing kit. Pass an explicit project;
preflight does not dispatch requests. `--keys` accepts a JSON array of exact
`native:`/`online:` entry IDs for a pilot. Keep the same kit and ledger throughout
the campaign, including retries. The helper uses POSIX file locking; run it on
Linux, macOS or WSL. `python3 test/test_translation_program.py` also exercises
its mocked budget and resumption contracts.

```sh
python3 tools/localization/google_draft.py --backend plain --output artifacts/localization-ta \
  --project PROJECT_ID --locale ta --keys artifacts/localization-ta/pilot-keys.json
# Add --execute only after checking the preflight estimate.
```

The helper uses English-to-target NMT, fills blank draft values only, and caches
provider output by source/protected literals, project, target, model and protection
version. Set catalog identity metadata deliberately before drafting. Its gross
ceiling is 400,000 request characters (US$8 at US$20/million, before taxes and
without assuming credits); `--max-characters` can lower but never raise that limit.
Each attempt reserves its entire character count durably before dispatch; failed
or uncertain requests remain charged against the ledger. Existing ledger ceilings
cannot be changed. Credentials are read from `gcloud` into memory and never written
to request/response evidence. Confirm current provider pricing before a campaign.

Damaged protected literals are reported in `repair_required`, retained in the
cache/raw response, and left blank in the draft for manual correction. They are
not silently retried or accepted. Inspect all completed output linguistically;
placeholder success and provider output are not review sign-offs. Re-running
resumes cached work and preserves nonblank edits. Do not remove the ledger or
cache to bypass a ceiling. If frozen sources change, preserve the old kit and
carry its ledger/cache into the new kit before continuing; stop using the old
kit for API calls so both kits cannot spend the same remaining allowance.

For the general gcloud-authenticated runner, use the same frozen kit and pass
its locale explicitly. The default command estimates a source-based pilot without
API calls; `--execute` makes paid requests. Review that pilot before bulk work:

```sh
python3 tools/localization/cloud_draft.py --output artifacts/localization-release \
  --locale ms --project YOUR_PROJECT_ID
python3 tools/localization/cloud_draft.py --output artifacts/localization-release \
  --locale ms --project YOUR_PROJECT_ID --execute
python3 tools/localization/cloud_draft.py --output artifacts/localization-release \
  --locale ms --project YOUR_PROJECT_ID --phase bulk --pilot-reviewed --execute
```

`--pilot-size` controls entries per source domain. Existing nonblank translations
are retained. See [Cloud Translation drafts](#cloud-translation-drafts) for current
budget, cache, protected-token recovery and locking behavior. Run
`python3 test/test_cloud_draft.py` when changing this runner.

The runners use separate caches and accounting formats. The general `cloud_draft.py`
runner does not share the Tamil helper’s hard budget ledger; do not use a separate
runner to bypass a campaign ceiling.

For Norwegian Bokmål HTML comparison drafts, prepare a scoped kit (for example,
`prepare --locales nb --output artifacts/norwegian/kit`), then estimate work without
network access:

```sh
python3 tools/localization/google_draft.py --backend html --output artifacts/norwegian/kit --locale nb
```

Authenticate `gcloud` to an existing project with Cloud Translation enabled. Pass
`--project PROJECT_ID --execute --max-characters LIMIT` to make paid Translation
v3 requests. `--price-per-million` changes the planning estimate; verify actual
project pricing before interpreting it as a cost forecast. The runner queries
supported languages and selects `nb` when available, otherwise Google's `no`
target for the Bokmål catalog. `--batches BATCH_ID ...` limits the source-based
pilot to selected manifest batches. Complete and review that pilot before running
the full catalog.

The runner protects substitutions, literal line breaks, authored markup and URLs
(including standalone relative routes and `www` addresses)
with non-translatable HTML spans and rejects changed tokens. Outer whitespace is
restored for message fragments. These checks do not establish correct meaning or
word order. Exact request/project/location matches reuse immutable provider
responses under the kit's `google/LOCALE/responses/`; generated JSON catalogs live
under `google/LOCALE/runs/PLAN_HASH/`. They never replace `drafts/`, registered
catalogs or review records. Copy a selected machine draft into the editorial
workspace only when intentionally beginning review.

The cumulative character allowance counts every attempted translation request,
including uncertain failures and retries, before dispatch. It conservatively
counts the protected HTML payload and persists across resumed invocations. Run
one writer per kit. Authentication tokens are cached in memory and never written
to evidence. Requests use bounded retries. Invalid responses remain inspectable;
`--retry-invalid-responses` archives them under `rejected/` before another paid
attempt within the same allowance. Allowances remain subject to the campaign’s US$8 hard ceiling and existing
ledger guards. Provider output remains a machine draft, never a linguistic sign-off.

The optional `--backend plain|html` selects either backend explicitly. Each keeps
its existing request/cache formats. A shared campaign lock and ledger guard reject
switching backends after paid or reserved work; use the original mode to resume
that campaign, without deleting or resetting its ledger.

The Marathi blank-fill workflow is selected explicitly and uses its own runner
and accounting format:

```sh
python3 tools/localization/google_draft.py --mode blank-fill \
  --output artifacts/localization-mr --locale mr --project PROJECT_ID --pilot
# Add --run only after checking the estimate and reviewing the pilot.
```

Without `--run`, this workflow estimates without sending translation requests.
Omit `--pilot` after review to process the remaining blank entries. It preserves
nonempty reviewed values, protects format tokens and literal invariants, segments
authored newlines, rejects damaged responses and retains valid siblings and cached
paid responses. Keep the helper as the exclusive catalog writer while it runs:
it checks draft hashes before replacement and aborts on external edits. Independent
editors do not participate in its lock.

Blank-fill uses the kit’s `google-budget.json` and an exclusive writer lock. Its
`--budget-usd` defaults to 9 and cannot exceed US$9. It reserves every submitted
protected payload code point at US$20 per million before dispatch, including
uncertain failures and retries, without assuming free credit. POSIX reservations
flush the file and directory after atomic replacement; durability errors stop
dispatch. Windows flushes file contents but has no portable directory-flush
equivalent. Text is UTF-8, and authentication tokens remain in memory.

The blank-fill ledger is distinct from the legacy plain/HTML campaign ledger
described above. Keep one accounting format and ledger for each campaign across
refreshed source kits; do not switch workflows or discard ledgers to bypass an
authorized ceiling. The Tamil campaign retains its US$8 ceiling. These are
accounted API-request limits, not guarantees about unrelated project spending or
an exported billing invoice. Provider output is never a linguistic sign-off.

Bulgarian diagnostic surfaces keep raw errors in the diagnostic log and display
an existing reviewed native message where available, otherwise the reviewed
online summary. `Glob2UI::DiagnosticText` keeps a bounded cache per UI context;
changing the diagnostic invalidates its display cache. It does not change model
readiness, error codes, or transmitted values. Script-editor statuses, cursor
coordinates and built-in Hive notices have explicit catalog keys. Hive's public
string report history remains unchanged; a private companion records which
messages were created locally. Remote reports and user text are never classified
by matching their contents. Editor preview status retains its raw event and
localizes it during rendering so changing language updates an existing notice
without restarting the engine.

For a Bulgarian Google Cloud draft, prepare a kit with `--locales bg`, then run:

```sh
python3 tools/localization/translation_program.py machine-draft \
  --output artifacts/translation-program --locale bg --project YOUR_PROJECT \
  --max-cost-usd 10 --rate-per-million 20
```

This development-only command uses `gcloud auth print-access-token` and Cloud
Translation Advanced v3 NMT. Confirm the current per-million-character price
before passing `--rate-per-million`; the default is USD 20. It requires POSIX
file locking. `--only-keys FILE` selects a JSON array of `native:[key]` and
`online:key` IDs for a representative pilot; `--limit-entries N` bounds the
number of blank values filled. Establish the pilot before translating the rest.
Frozen source contracts, protected tokens, validated response caches and raw
provider responses stay in the kit. Provider drafts are separate from editable
catalogs, and resuming fills only blank values. Every attempt reserves its full
estimated cost durably before sending, including retries and ambiguous failures;
monthly free credits are not assumed. The ledger covers this kit, not unrelated
project usage. Reuse the same kit when resuming to retain its budget history.
Damaged protected tokens or invalid responses stop drafting and retain evidence;
review the affected entry before resuming rather than guessing a repair.

The same explicit drafting command supports the legacy native locales when new
shared keys need translations. Use canonical locale tags in `--locales` and
`--locale`; native filename aliases are mapped separately. In particular,
`pt-BR` writes `texts.br.txt` (Brazilian Portuguese), while `pt-PT` writes
`texts.pt.txt`. Canonical `br` would mean Breton and is rejected. Inspect each
catalog's language metadata before adding a mapping. This opt-in scope does not
change the default release-language set or certify an unfinished online catalog.
When a source expansion needs a fresh immutable kit, carry the complete cumulative
cost ledger and provider evidence forward before any further request; a new
output directory must not reset the task's spending budget.

Bulgarian additionally requires an independent adversarial pass over every batch,
after semantic review and editorial correction. Use standard Bulgarian Cyrillic
(`bg`, `Български`), polite plural instructions, and concise action labels. Flat
game messages with varying quantities use count-neutral labels or abbreviations;
check actual callers at 0, 1 and 2 rather than assuming every number is plural. Game
terms include `работник`, `разузнавач`, `воин`, `рояк`, `хан` and `флаг`; read
actual gameplay usage and Bulgarian dictionary entries before resolving ambiguous
labels. English controls meaning; other locale catalogs provide comparisons.
Agents must review every key and final plural variant, record their actual
coverage and recheck substantive edits. These records establish agent review,
not native-human certification.

For Telugu Google NMT comparison drafts, use the development-only adapter after
preparing a Telugu kit and its blank draft:
`python3 tools/localization/google_draft_telugu.py --output artifacts/localization-telugu --project PROJECT_ID --pilot`.
This is a dry run; add `--run` to send requests using the current `gcloud auth`
identity. Supply the explicit project ID whose display name is `Bradley personal`
(or pass its exact display name with `--expected-project-name`). The adapter never
changes the active gcloud project, enables services or configures billing. The
Cloud Translation API and billing must already be enabled on that project.
Review the representative pilot before running again without `--pilot` to fill
the remaining blank entries. Existing nonempty draft entries are retained.

The adapter uses v3 English-to-Telugu `general/nmt`, initializes blank target
language metadata, and protects placeholders, product/AI names, URLs, unambiguous
paths, escaped paragraph breaks and online literal invariants with HTML
notranslate spans. It validates their exact restoration before writing catalogs.
Malformed responses remain evidence and
block the run. Raw requests/responses live under the kit's `google/responses/`;
successful cached inputs, including pilot inputs, are reused without more calls.
An exclusive process lock serializes adapter runs. The durable `google/ledger.json`
reserves each network attempt before sending, including attempts whose outcome
is unknown; failures never refund reservations. The gross ceiling is $9 at
$20 per million Unicode characters, conservatively counting all submitted HTML
and ignoring free credits. Confirm
[current provider prices](https://cloud.google.com/products/translate/pricing)
before using this rate-based guard. The ledger covers only this adapter's calls, not unrelated
project spending. Retain the ledger and response directory together; deleting
them defeats resumability and the campaign spending guard. The adapter is a
Linux contributor tool and requires Python's `fcntl` locking support.

Register finished catalogs in both inventories and their browser loaders, then
test system-language selection against the installed catalog set. Mapping a
system tag alone must never enable an unfinished locale. Norwegian Bokmål uses
`nb`; generic `no` selects Bokmål, while explicit `nn`, legacy `no-nyn` and
`no-NO-NY` preferences fall through to the next supported language. Punjabi scripts use
`pa-guru` and `pa-arab`; explicit scripts take precedence over country defaults.
The Latin-script Pulaar catalog uses game code `fuc` and online tag `fuc-Latn`.
Generic Fula tags resolve to it only for Senegal or Mauritania; Adlam and other
Fula varieties must not be silently advertised as covered.

The installed native catalog list also defines the language tokens accepted by
the legacy SGSL lexer, including bare codes in `show("text", ms)`. Registering a
new code therefore expands map/script acceptance even when no renderer or game
rule changes. Bump `SIM_REVISION` and regenerate the golden match record/trace in
the same change. Keep existing catalog order stable, verify saved SGSL source
bytes and program counters across registration, and test the affected replay and
network acceptance boundaries. This does not by itself change the save format or
justify moving its minimum supported version.

Each language needs a brief identifying its actual variety, script and region,
with cited orthography, grammar and dictionary sources and terminology rationales.
Unresolved uncertainty blocks finalization. Establish a source-based pilot before
bulk translation, especially for languages with limited reference material. A
backtranslation can expose a problem but is not evidence that a phrase is idiomatic.
For disputed wording, compare actual dictionary or grammar entries, contemporary
software usage and the game context. Record the consulted passage or lookup,
locale variety and proposed interpretation; a search result or an unread source
is not an attestation. Check reference material itself for template contamination
or examples from another language. Google Translate or another provider may
supply a comparison draft, but record its actual output and do not treat agreement
between translation engines as independent linguistic approval. Reopen review
after substantive corrections and repeat until meaning, idiom and terminology
issues are resolved; retain uncertain terms as blockers rather than guessing.

Assign distinct author, reviewer and editor identities per batch. The reviewer
checks every key against the source and gameplay; the editor examines every key
for idiom, register, consistency and length. `edited_keys` records that editorial
pass, including text retained unchanged. After editing, the independent reviewer
rechecks every final key. Record source SHA-256 and final catalog hashes in the locale record; each batch
records the source hash and `target_sha256`, a hash of its translated key/value
subset. Only edited batches need renewed linguistic sign-offs; release gates bind
the complete final catalog hashes. Also record `reviewed_keys`, `edited_keys`,
`rechecked_keys` and `rechecked_by`. Changed hashes invalidate stale sign-offs. `batch_target_hashes` computes each
batch digest. English-identical text needs an individual `accepted_english` entry
with the exact review-queue ID and a nonempty rationale; blanket exceptions are
not accepted. `status` validates actual draft completeness before considering
review records.
Low-resource locales additionally require a complete adversarial pass by a fourth
identity, recorded as `adversarial_reviewer` and `adversarial_reviewed_keys`.

Findings identify the source key, severity, state, and reason for any accepted
minor issue. No critical/major issue or linguistic uncertainty may remain open.
`status --locale hi` audits these independently supplied records and requires
evidence for the language brief, pilot, structural checks, rendering, browser
startup and platform gates. Each passed gate includes the source/catalog hashes
and evidence objects with `path` and `sha256`; relative paths resolve within the
kit directory. Missing or changed evidence cannot pass. It never generates approvals or infers linguistic
review from filled entries. An agent review is not native-human certification;
describe it as agent-reviewed and keep unverified platforms explicit.

Build `translation-preview-harness` for unregistered catalog captures. Its
`matrix ROOT INPUT OUTPUT WIDTH HEIGHT` mode uses the actual game font and shared
wrapper at authored sizes 10, 13 and 20 and raster scales 1 and 2. Optional
`SIZE SCALE` arguments select exactly one of these combinations for independently
resumable batches; omitting both runs all six. Each batch has its own complete
manifest and uses the same wrapping and rasterization. `screen` adds a screen name
(`main`, `settings`, `settings-audio`, `settings-ais`, `settings-online`,
`settings-player`, desktop-only `settings-controls`, `lobby`, `lobby-players` or
`new-map`). The AI library capture uses its real buttons and an unavailable
loopback server, so it verifies local labels without remote account or provider
actions. For controlled API-error captures, `settings-ais` also accepts
`GLOB2_TRANSLATION_PREVIEW_HTTP_ORIGIN=http://127.0.0.1:PORT` together with
`GLOB2_TRANSLATION_PREVIEW_EXPECT_NOTICE`. It pumps the production client and
requires that exact notice in the screen's accessible text before capturing.
Record the local fixture response and request receipts separately; these captures
do not verify a production server. `game` adds
`gameplay`, `statistics`, `tutorial` (the introduction), or `tutorial:LESSON-ID` and captures
the real engine and menu dialog. Statistics captures navigate the real Overview, Economy and World topics and available metric history/details. They retain live-player visibility rules and never synthesize history or expose hidden win probabilities. Tutorial IDs must belong to the current shipped
lesson catalog. When a lesson opens its automatic briefing, the harness saves
that state separately, dismisses it and captures the uncovered guidance panel.
Each gameplay capture checks that drawing leaves the simulation checksum intact.
A published tutorial model alone does not prove that its text is visible. Inputs are
`native.json` and `online.json` key/value objects plus `metadata.json` identifying
the locale. Preserve the original text-catalog hashes in that metadata when
converting formats. A small pilot is a font diagnostic, not evidence that the
whole catalog was displayed. `editor ROOT INPUT OUTPUT WIDTH HEIGHT` captures the
production map editor, terrain controls and menu on a blank map without stepping
the simulation. Setup and menu navigation use controlled harness friend access;
captures use the production editor drawing path. It uses the portable SDL renderer; set `GLOB2_MOBILE_UI=1` to
inspect the phone presentation. These resized desktop captures do not establish
rendering on an actual mobile device. `script-editor ROOT INPUT OUTPUT WIDTH HEIGHT`
opens the production scenario editor and captures its localized cursor, successful
JavaScript validation and a controlled syntax failure. It validates temporary
source without confirming or saving the scenario and checks that map state is
unchanged. Use a fresh profile with `textSizePercent=100` or `150` to exercise
normal and enlarged text; record the profile, viewport and presentation settings
with the captures.

wrapper at several authored sizes and raster scales. `screen` adds a screen name
(`main`, `settings`, `settings-audio`, `settings-player`, `lobby` or `lobby-players`); `game` adds
`gameplay`, `tutorial` (the introduction), or `tutorial:LESSON-ID` and captures
the real engine and menu dialog. `statistics` selects the win-probability metric
on the real results screen for the current local map; it does not represent a
completed match or validate prediction accuracy. `ingame-statistics:TOPIC` captures
the actual live sidebar or compact sheet for `overview`, `colony`, `economy`,
`military` or `world`, with published local-game history.
`ingame-statistics-history` opens the real population-history dialog;
`map-overlays` captures the desktop overlay-tools view. These modes use ordinary
presentation navigation and preserve the simulation checksum while drawing.
Tutorial IDs must belong to the current shipped
lesson catalog. For a nonempty current mission briefing, the harness captures
the real dialog separately, opening it for preview if scene admission skipped
the automatic opening. It records preview-opened and automatic origins separately,
then dismisses the dialog and captures the uncovered guidance panel.
Legacy tutorial highlight arrows wait while a modal is open, then resume after
dismissal so they cannot cover briefing text. The desktop `tutorial:swimming`
preview at 800×600 also toggles a real highlight and compares the actual modal's
pixels before, during and after the probe, preserving the simulation checksum.
It retains the initial presented frame and requires two unchanged baseline frames
before changing the highlight, distinguishing first-frame composition from arrow
interference.
The gameplay preview also captures the win-probability statistics screen and its
metric explanation, checking that drawing leaves the simulation checksum intact.

> > > > > > > A published tutorial model alone does not prove that its text is visible. Inputs are
> > > > > > > `native.json` and `online.json` key/value objects plus `metadata.json` identifying
> > > > > > > the locale. Preserve the original text-catalog hashes in that metadata when
> > > > > > > converting formats. A small pilot is a font diagnostic, not evidence that the
> > > > > > > whole catalog was displayed.

Tutorial captures also click the actual guidance controls to record the collapsed
control and re-expanded instructions. Those UI actions and draws must leave the
simulation checksum unchanged; offscreen phone presentation does not establish
mobile-platform or physical-touch hardware coverage.

Capture evidence includes the executable hash and compiled build provenance,
as well as current source, catalog and font hashes. Freeze sources during builds
and captures, and verify the compiled source identity before accepting evidence;
current files alone cannot establish what an older executable contains. Keep
platform, viewport and phone-presentation claims separate.

Use a concise, respectful contemporary register consistently across game and
online surfaces. Preserve placeholders and technical identifiers. Match repeated
game concepts across both catalogs: workers gather and build, explorers scout,
warriors fight, flags assign work or attention, Inns attract converts and Markets
exchange fruits. Translate ambiguous terrain terms from their control's effect,
not an isolated dictionary sense. Preserve tutorial execution and campaign IDs;
only their presentation is localized. In compact scenario editors, wrap compiler
notices as paragraphs and size touch tab/action columns from actual label-word
widths, retaining their control IDs and minimum hit areas. Empty statistics-chart
notices wrap within the chart using the same font and shaping boundaries as the
rest of the chart. Review legitimate names and loanwords
explicitly rather than changing correct text merely to avoid an English match.

### Google NMT draft preparation

After preparing a frozen kit and blank draft, use
`python3 tools/localization/google_draft.py --mode editorial --output artifacts/translation-program --locale fil --pilot 42`
for a pilot spanning source domains and both surfaces. Omit `--pilot` to resume
the remaining entries. The runner uses the existing `gcloud` login and defaults
to the Bradley Personal project; `--project` selects another explicitly authorized
project. It uses the NMT model with English as the source language.

The runner protects tokens and invariants, retains raw responses, and caches
identical requests. Nonblank draft values are retained so resuming cannot undo
editorial work. Inspect rejected entries rather than repeatedly requesting them.
The local attempt ledger reserves estimated charges before each request, including
failed attempts, without assuming monthly credits. `--max-usd` bounds cumulative
estimated charges, including HTML protection overhead; recheck provider pricing
before use. Interrupted runs retain their evidence and may leave a lock that must
be inspected before removal. These drafts still require the independent review
and rendering gates above.

### Filipino terminology

Filipino (`fil`, including the `tl` system-language alias) uses contemporary
standard Filipino in the Latin script. Use direct, respectful instructions and
recognizable technical loanwords rather than awkward literal translations.
Follow the [KWF orthography guide](https://kwfdiksiyonaryo.ph/gabay) for retained
foreign spellings and affixes; ordinary UI text does not need dictionary stress
marks.

Filipino uses the existing Latin font with authored drawing advances, so larger
raster sizes retain the widths used for wrapping. Check both ordinary and
high-density rendering when changing its font configuration.
Check both catalog surfaces against the full font and browser startup subset,
including shared UI symbols such as the fullwidth plus control.

Use **manggagawa** for gathering/building workers, **mandirigma** for warriors,
**tagapaggalugad** for explorers, **Pugad** for the unit-producing Swarm building,
**Panuluyan** for Inns, **Pamilihan** for Markets, and **bandila** for flags.
Explain unit conversion as joining another team. Distinguish credits acknowledging
contributors from spendable studio credits, and computing workers from colony
units. Consult gameplay context when these terms occur in other senses.

For classic terrain looks, use **Mayabong na damo**, **Tuyong damo**,
**Buhangin ng burol**, and **Maputlang buhangin**. The dune label follows the
existing **Burol ng buhangin** theme name; these looks retain ordinary grass or
sand rules and do not imply an elevation mechanic.

Review complete sentences and assembled browser fragments. Preserve necessary
boundary spaces, but avoid spaces introduced around protected placeholders before
punctuation. Numeric counts usually take an unchanged noun; do not mechanically
add `mga` or assume the runtime's `one` plural category means exactly one.

### Cloud Translation drafts

Prepare a Bengali kit with `prepare --locales bn --output
artifacts/bengali-localization` and `draft --locale bn --output
artifacts/bengali-localization` using the translation-program tool above. Then
`python3 tools/localization/cloud_draft.py --project PROJECT_ID` estimates a
Bengali pilot without sending a paid request. Add
`--execute` to request the pilot through Translation API v3 using the active
`gcloud` credentials. Review the pilot before running `--phase bulk
--pilot-reviewed --execute`. `--output` selects the kit and `--locale` selects its
language. Existing translations are never overwritten.

The runner defaults to a US$10 ceiling and accounts conservatively at US$20 per
million NMT input characters without assuming free credits. Every request attempt
is reserved before transmission, including retries whose outcome may be unknown.
Verify the current [NMT tariff](https://cloud.google.com/products/translate/pricing)
before requesting paid drafts; update the accounting rate if it changes.
Successful responses are cached and checkpointed. Protected-token failures retain
the response and block retransmission until a reviewer supplies a correction;
formatting-only templates bypass translation. Keep the accounting and response
files with the kit when resuming. A leftover runner lock needs investigation
before removal. Credentials are never written into the kit.

### Bengali conventions

Bengali (`bn`, বাংলা) uses contemporary standard written Bengali for readers in
Bangladesh and West Bengal, in Bengali script and logical left-to-right order.
Use respectful আপনি/করুন forms and concise familiar wording, with consistent
terms across lessons, game controls and the online platform. Preserve product
names, code, filenames, shortcuts and formatting tokens. Dynamic online numbers
continue to use the existing locale formatting functions.

The concept anchors are শ্রমিক (Worker), অনুসন্ধানকারী (Explorer), যোদ্ধা
(Warrior), সরাইখানা (Inn), মৌচাক (the Swarm building), বাজার (Market), and পতাকা
(Flag). A moving swarm may be a ঝাঁক; the unit-producing building is a মৌচাক.
Describe assignment targets as requested workers rather than workers already
present. Terrain passes mean গিরিপথ, while river banks mean নদীর পাড়.

The pinned Noto Sans Bengali asset supplies shaping tables for HarfBuzz in the
game and the shared browser font CSS. Retain those tables and the OFL licence in
packages. Check conjuncts, reph, vowel signs, mixed Latin text and Bengali digits
at small sizes; glyph coverage alone does not establish correct shaping or fit.
Explicit non-Bengali script tags must not select the Bengali-script catalog.
After adding or changing a language's displayed name, regenerate the checked-in
browser assets with the pinned exporter command in `browser/derive_assets.py`.
Its font fingerprint includes the language-name inventory even when the resulting
subset font bytes stay identical.

### Malay terminology

The `ms` catalog uses standard Malaysian Malay in Latin/Rumi script, displayed
as **Bahasa Melayu**. Explicit Jawi/Arabic-script locale preferences do not select
this catalog. Use contemporary Malaysian software terms: **Tetapan**, **fail**,
**akaun**, **kata laluan**, **pelayar web**, **muat turun** and **muat naik**.
Indonesian translations can clarify game concepts, but are not Malaysian wording
authorities. Consult [DBP's dictionary and terminology](https://prpm.dbp.gov.my/)
for disputed words and review each term in its actual gameplay context.

| Concept | Malay (`ms`) |
| --- | --- |
| Worker / Explorer / Warrior | Pekerja / Penjelajah / Pahlawan |
| Inn / Swarm / Market | Penginapan / Sarang / Pasar |
| Racetrack | Litar Larian (unit training, not horse racing) |
| Room / Match | Bilik / Perlawanan |
| Map generator / Terrain | Penjana peta / Rupa bumi |
| Stream reach / Sand bar | Panjang anak sungai / Beting pasir |

Keep building names consistent in lessons and HUD text. The Inn supplies food,
the Swarm produces units, and flags assign work or military attention. Music
playback uses **Sedang dimainkan**, rather than the verb for playing a game.

### Hindi, Marathi, Urdu and Swahili terminology


Use these concept anchors across the game, lessons and online messages. Inflect
words naturally in sentences rather than substituting labels mechanically.
Hindi uses contemporary Devanagari, Urdu uses Arabic script with logical text
order, Marathi uses contemporary standard Marathi in Devanagari, and Swahili
uses standard Latin-script Kiswahili. Marathi instructions use respectful plural
imperatives such as करा and निवडा; shared script does not justify copying Hindi
grammar. Inns feed units, while Swarms are the hive buildings that create units.

| Concept | Hindi (`hi`) | Marathi (`mr`) | Urdu (`ur`) | Swahili (`sw`) |
| --- | --- | --- | --- | --- |
| Worker | श्रमिक | कामगार | کارکن | Mfanyakazi |
| Explorer | खोजी | शोधक | کھوجی | Mgunduzi |
| Warrior | योद्धा | योद्धा | جنگجو | Shujaa |
| Inn | सराय | खानावळ | سرائے | Nyumba ya wageni |
| Swarm | छत्ता | पोळे | چھتہ | Kiota |
| Market | बाज़ार | बाजार | بازار | Soko |

An assigned worker target requests workers; it does not count workers already
present. Construction and operating supply use separate targets. Keep those
meanings distinct in instructions. Reviewed product names, technical terms and
loanwords belong in the existing shared-value exception contracts; an exception
for one key does not justify leaving unrelated prose in English.

### Telugu terminology

Telugu (`te`) uses modern standard Telugu in its own script, with concise,
respectful wording accessible across Andhra Pradesh and Telangana. Keep the
following role and building names consistent in menus, tutorials and online
messages; inflect them naturally in sentences.

| Concept | Telugu |
| --- | --- |
| Worker | కార్మికుడు / కార్మికులు |
| Explorer | అన్వేషకుడు / అన్వేషకులు |
| Warrior | యోధుడు / యోధులు |
| Inn | సత్రం / సత్రాలు |
| Swarm | గూడు |
| Market | మార్కెట్ |
| Flag | జెండా |
| Map | మ్యాప్ |
| Unit | యూనిట్ |
| Settings | సెట్టింగ్‌లు |
| Building upgrade | మెరుగుదల |
| Music: Calm | ప్రశాంతం |
| Music: Building | నిర్మాణం |
| Music: Combat | పోరాటం |

Reachability messages describe physically reaching a building, flag or resource,
not software access. Conversion means a unit joining another team; explain that
action instead of using an ambiguous exchange term. Flags assign work or
attention, and the Swarm is the colony's unit-producing nest. Retain product and
AI names consistently. Familiar software loanwords are acceptable when they make
the interface easier to understand; avoid mechanically transliterating role
names that already have clear Telugu terms.

Music mood names identify soundtrack sections, rather than human emotions.
When a message inserts a translated model, library category or product name,
use a neutral label or colon when its grammatical suffix cannot be chosen
reliably. Reporting metric name slots contain category or product names;
they are not numeric count placeholders.

### Tamil terminology

Tamil (`ta`, தமிழ்) uses broad contemporary written Tamil in logical Unicode
order. Use respectful instructions and natural inflection across game and online
surfaces. The [Microsoft Tamil style guide](https://aka.ms/tamil-styleguide)
provides software-localization guidance; the [Unicode Tamil chapter](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-12/#G12322)
describes the script's vowel and puḷḷi behavior.

| Concept                  | Tamil            | Context                                                           |
| ------------------------ | ---------------- | ----------------------------------------------------------------- |
| Worker                   | தொழிலாளர்        | Gathering and building unit                                       |
| Explorer                 | சாரணர்           | Scout, rather than scientific researcher                          |
| Warrior                  | போர்வீரர்        | Fighting unit                                                     |
| Player                   | ஆட்டக்காரர்      | Person playing, distinct from a warrior unit                      |
| Unit                     | அலகு             | Any unit class                                                    |
| Swarm                    | கூடு             | Unit-producing hive building, rather than a crowd                 |
| Inn                      | உணவகம்           | Feeds units and attracts converts                                 |
| Barracks / Training Camp | படைமுகாம்        | Same build-menu control and warrior-training building             |
| Colony                   | குடியிருப்பு     | Settlement                                                        |
| Clearing flag            | வள அகற்றும் கொடி | Removes resources, rather than removing the flag                  |
| Replay                   | மறுபதிவு         | Recorded match playback                                           |
| Product credit           | வரவு             | Usage balance; distinct from debt or contributor acknowledgements |
| Commander                | கட்டளை உதவியாளர் | In-game AI assistance role; Hive Mind remains the product name    |
| Studio                   | படைப்பகம்        | Online authoring workspace                                        |
| Online instance          | சேவையகம்         | Independent online service installation                           |

Keep keyboard key names and protocol identifiers recognizable. Translate terrain
controls from their effects, and preserve distinctions between requested workers
and workers present, capabilities and completed actions, and unit conversion and
religious conversion. Account wording such as கணக்கு and கடவுச்சொல் follows
established [Tamil software usage](https://support.google.com/accounts/answer/27441?hl=ta).
Shared mute/unmute controls cover both audio and chat moderation; keep their
Tamil action labels neutral rather than making every occurrence audio-specific.

## Anatomy of a screen

```cpp
class LANFindScreen : public Glob2UI::Screen
{
  public:
	explicit LANFindScreen(GAGGUI::ScreenStack &screens);
	Glob2UI::Element build(const Glob2UI::Presentation &p) override;
	void setServer(const std::string &address); // harness entry point
	void connect();

  protected:
	void onEscape() override { endExecute(QUIT); }

  private:
	std::string serverName = "localhost", playerName;
	std::vector<std::string> games;
	int selectedGame = -1;
};

Element LANFindScreen::build(const Presentation &p)
{
	auto fields = form({
		field(tr("[svr hostname]"), textField("server", serverName, [this](const std::string &v) { serverName = v; })),
		field(tr("[player name]"), textField("player", playerName, [this](const std::string &v) { playerName = v; })),
	});
	auto list = listView("games", games, selectedGame, [this](int i) { selectedGame = i; invalidate(); });
	return page(tr("[find LAN games]"), column({fields, expanded(list)}, {p.pt(8)}),
				actions({{"connect", tr("[connect]"), [this] { connect(); }, true, SDLK_RETURN},
						 {"back", tr("[goto main menu]"), [this] { endExecute(QUIT); }, false, SDLK_ESCAPE}},
						p),
				p);
}
```

`Glob2UI::Screen` paints the live colony background and keeps the frontend theme
active for its lifetime. Startup prepares the colony's first scene before showing
the menu; colony backdrops never show a scaled screenshot first. Later colony
scenes are prepared asynchronously. `Glob2UI::Dialog`
is the same for frontend modals; `Glob2UI::InGameDialog` takes a `Surface`
(`Match` by default, `Editor` for editor dialogs) and wears the theme
`themeFor()` maps it to, on every host. On big
desktop windows whose interface scale follows a 100 % desktop, `Glob2UI::Screen`
enlarges points and text up to 1.5x (`ui::comfortScale`, via
`UIScreen::adjustPresentation`); gameplay and dialogs keep their sizes. A screen holding unsaved work overrides `interceptsQuit()`: while
it is the running top screen, `SDL_EVENT_QUIT` (window close, Cmd+Q) reaches its
`handleExecutionEvent()` instead of stopping the stack, and the screen ends with
`QUIT_APPLICATION` once the player decides; `SDL_EVENT_TERMINATING` is never
vetoed. `endExecute(code)` and the
`ScreenStack` completion callback remain the navigation contract; push child
screens onto the stack rather than running them inline.

### Buffered input completion

Hosts can inject an additional event batch through `ScreenStack::AdditionalInput`.
The stack prepares it after ordinary input and viewport changes, runs one screen
update, and reports the prefix that the consumer actually processed. The completion
can arrive on a later GUI tick. Enqueuing an event is not a dispatch acknowledgement.

Screens that buffer events must override `defersExecutionInput()` and
`hasPendingExecutionInput()`, and implement `beginAdditionalInput()` and
`endAdditionalInput()` to retain the batch's `InputDispatchReceipt`. Resolve its
`consumed` count only when the event loop consumes that range. Check cancellation
before processing, erase only the cancelled range, and resolve it with zero;
suspension, viewport changes and teardown must also resolve outstanding receipts.
`EventQueue::erase()` preserves text storage for the remaining human events.
`GameSessionScreen` and `MapEditorScreen` provide the reference implementations.
The [MCP UI server](mcp-ui-server.md#extension-boundary) uses this boundary for
ordered input, release cleanup and capture after actual dispatch.

### Presentation

`Presentation` is resolved once per frame from the graphic context and passed
to `build()`:

| Member                                       | Meaning                                                                                                                                        |
| -------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `viewport`, `safe`, `dialog`                 | Whole surface; minus platform gutters; minus the onscreen keyboard. Content goes in `safe`, forms and dialogs in `dialog`.                     |
| `unit`, `pt()`                               | Logical pixels per point and the conversion.                                                                                                   |
| `textScale`                                  | Text enlargement over the fonts' authored size: the player's text size, times the theme's `touchTextScale` on touch hosts.                     |
| `textGrowth`, `textPt()`                     | The player's text size alone (1 at 100%), and a length that grows with it, for widths that hold text such as a label column.                   |
| `textUnit`                                   | Logical pixels per authored font pixel, used by the text measurer and canvas: `unit × textScale` on touch hosts, `textScale` on pointer hosts. |
| `touch`, `hover`                             | Touch capability (48-point targets) and pointer availability (tooltips).                                                                       |
| `compact()`, `expanded()`                    | Width class thresholds at 600 and 960 points.                                                                                                  |
| `phone()`, `landscape()`, `shortLandscape()` | Device shape helpers for structural choices.                                                                                                   |

### Text size

Fonts are rasterized at authored pixel sizes, while the logical surface differs
between screens: gameplay and the editor keep an 800x600 minimum, so on a phone
their `unit` is 1.5–2 while menus run at 1. Touch text is therefore sized in
points, like every other metric: the framework measures and draws it at
`p.textUnit`, and glyphs are rasterized at the size they are drawn
(`TrueTypeFont::updateRenderScale`). One text size then holds on every touch
surface. Pointer hosts keep the authored pixel sizes times the preference, so
desktop layouts are unchanged at 100% (`applyTextSize()` holds the rule).

- The player's preference (`Settings::textSizePercent`, 100–150%, in Settings
  > Display and the in-game options) sets `GAGCore::userTextScale` through
  > `Settings::setTextSizePercent()`. Every host picks it up on its next frame and
  > relayouts; screens need no code for it.
- Themes carry the touch base (`Theme::touchTextScale`, 1.15 for the game's
  themes), so menus, dialogs over gameplay and the end-of-game sheet match.
- Bespoke touch painters (the gameplay HUD, the phone editor) draw text at
  `gfx->textUnitsPerPoint()` times a role factor, never at
  `logicalUnitsPerPoint()`, and grow rows that are sized by their text with
  `InGameTouchTheme::textGrowth()`.
- A control's measured size must hold the text it paints at every text size.
  `libgag/src/ui/UILayoutHarness.cpp` and the presentation harness check that no
  painted line crosses a control's edge, at 100% and 150%.

Mobile game-event notifications start below the statistics, minimap and selection
header and building inventory, inside the safe-area gutters. Their text uses the
same normal font and .8 text role as the building title and inventory, respects
the player’s text-size preference, and wraps over a translucent background. Each
card vertically centers its icon and complete text block with equal top and bottom
padding. The feed is bounded
above the action bar and to 180 points of height; a final wrapped row is shortened
with an ellipsis when space runs out. Click-to-jump rectangles follow the scaled,
wrapped rows. Desktop notifications retain their original placement and sizing.

### Building assignment preferences

Settings > Buildings enumerates variants from the installed building catalog;
construction and upgrade controls follow explicit variant links. Assignment and
attraction-radius bounds come from each descriptor. Initial assignments come
from `presentation.defaultAssigned`, and initial radii from the descriptor's
`defaultUnitStayRange`. Remembered choices are stored by catalog fingerprint and
stable variant key, so custom catalogs that reuse stock names cannot overwrite
stock preferences. A changed catalog starts from its authored defaults. Version
1 preferences import their historical family/level slots through the frozen
stock catalog; current preferences and per-game overrides use stable keys.

The desktop building panel records metric row positions while drawing. Upgrade
values reuse those positions inside the scrollable body; newly introduced
metrics and construction costs appear after the current controls. Repair costs
use the same body area. The mouse wheel remains active over a hovered upgrade
or repair button, so long previews stay accessible. Physical attraction buildings
put their attraction counts below the HP/inside header; drawing and click handling
share the additional height. A zero initial attraction radius still exposes the
range control when the configured maximum permits adjustment.

### Map scrolling preferences

Settings > Controls has independent edge scrolling toggles for fullscreen and
windowed mode, defaulting to on and off respectively. They apply to both the game
and map editor using the current display mode; keyboard and drag panning remain
available in either mode. These are local preferences, saved in `preferences.txt`.

### Containers and controls

Containers: `column`, `row`, `stack`, `wrap` (grid by minimum child width),
`scroll` (vertical, owns wheel, pan, scrollbar and scroll-into-view),
`adaptive`, `footer`, `card`, `form`/`field` (label beside the control when
wide, stacked when narrow), `padding`/`pad`, `align`/`center`, `sized`,
`width`, `height`, `maxWidth`, `expanded` (flex), `spacer`, `divider`.

Controls: `label`, `heading`, `title`, `caption`, `paragraph`, `button`,
`toggle`, `choice` (popup dropdown), `chooser` (opens a child screen),
`segments`, `stepper`, `slider`, `textField` (single line, IME composition,
browser text input), `textEditor` (multi-line), `listView` (virtualized rows
with selection and activation), `progress`, `image`, `sprite`, `swatch`,
`icon`, `canvas` (custom painter with pointer and wheel callbacks).

Game-side builders in `Glob2UI` reproduce the game's established look on
each host, so a screen written with them looks as it did before the
framework:

- `page()`: on pointer hosts the centered 640x480 paper panel with a
  `pageTitle()`, a scrolling body and the action row at the bottom right; on
  touch hosts a content-sized card with a folding footer.
- `menu()`: on pointer hosts a narrow panel of 300-point body-font buttons
  with the Escape action pinned at the bottom; on touch hosts a grid of large
  actions.
- `actions(items, p)`: content-sized body-font buttons at the right, wrapping
  when their measured labels exceed the available width; on touch hosts a
  wrapping grid of large buttons.
- `pageTitle()` and `hint()`: see Typography.
- `animation()` and `mapPreview()` (hosts a `MapPreview` in a canvas).

Settings use leading labels and help text with trailing value controls. Checkbox
rows use `ToggleOptions::trailing` and `help` so the label, help and mark share
one pointer, touch and keyboard target. Wide rows reserve the same control column
as other settings fields; narrow rows keep a trailing checkbox beside wrapping
captions while larger controls stack below their labels. Keep every category's
sidebar icon distinct: recording uses Play, custom AIs Robot, map generators Map,
and MCP uses Server.

### Typography

TrueTypeFont measures shaped text using SDL_ttf metrics at the authored font size,
independently of bitmap rendering. Its metrics LRU is bounded to 1,024 entries and
1 MiB of stored key text; color and drawable scale do not affect its keys. Font
replacement/reload clears these metrics. Layout measurement neither creates
bitmaps nor evicts rendered text. Use bounded `listView` controls for large
fixed-height data lists, with full text in a separate details area when needed;
wrapping every entry in a `scroll` still measures all entries during layout.
Bulgarian opts into authored logical draw advances alongside other new locales,
keeping the complete high-resolution raster within measured widths while
preserving existing locale geometry.

Script-layout locales, Filipino, Swahili, Norwegian Bokmål and Malay draw high-DPI text
at the authored width used by wrapping and hit testing. The complete
high-resolution bitmap is resampled into that destination; font hinting at a
larger raster size must not make a fitted line overflow its layout. Legacy
Latin locales retain their existing geometry.

Offscreen text that will be transformed must use `Font::rasterizeString` with an
explicit physical scale; ordinary offscreen `drawString` retains the authored
resolution. The touch inspector's curved labels rasterize their source at twice
the final physical text size before warping, while keeping authored metrics for
button fitting and display-density textures for the finished labels.

The phone HUD's bottom navigation reserves its icon band and measures every
translated, wrapped caption using the active font and text-size preference.
Captions taller than the original 16-point slot expand the action strip upward
with bottom padding. When whole words cannot fit across six buttons, navigation
uses balanced rows of three, two or one button. Normal-size captions retain the
six-button row when they fit. Drawing, pointer input and keyboard targets share
the same button rectangles; world bounds end above the resulting strip.
Do not shorten a translation to compensate for a fixed-height caption clip.
Only Bulgarian single-word captions may reuse one point from each inset and
shrink to at least 10px at 100% text size, retaining the player's text multiplier
and conservative original strip measurement without moving icons or hit areas.
Touch text wrapping preserves the active font's shaped clusters, including an
indivisible cluster wider than its slot, and retains explicit blank lines.
Navigation measurements keep one last result per HUD, invalidated by caption,
locale, font metrics, width and scale changes; drawing uses the same wrapping.

The small map-zoom bar measures its translated captions within the available
sidebar or screen width. Its drawing and click handler share one bounded layout;
button boundaries remain stable when the zoom changes between Fit and 100%.
Long captions may use spare readout space and scale within their own clip, while
taller fonts increase the bar height. Keep caption fitting and hit geometry
together in `src/render/overlay/MapZoomControls.h`.

Every screen uses the same scale, so menus read alike:

| Use                                           | How                                                                                                  |
| --------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| Page title                                    | `pageTitle()` (Heading, left-aligned); `page()`, `menu()`, Settings, the hub and the room all use it |
| Hero content                                  | `title()` (Title): the main-menu wordmark fallback, the result banner, large numbers                 |
| Section header                                | `heading()`, never a plain `label()`                                                                 |
| Body, list rows, fields, tabs, action buttons | Body (the default)                                                                                   |
| Hints, help and notes                         | `hint()` (Support, muted)                                                                            |
| Small metadata                                | `caption()`                                                                                          |

Only the main menu's launch buttons use the Heading font for buttons.

### Theme

`Theme` carries a palette of semantic colour tokens (ink, muted, paper, panel,
field, rail, line, accent, accentInk, selected, hover, focus, scrim, disabled,
danger, success, warning, shadow, pressed, backdrop, neutral, placeholder), the
HUD tokens drawn outside the controls (`hud`: ink, paper, field, selected,
border, readout, the dial colours, destroy, erasePreview), the menu `backdrop`,
font roles (Title, Heading, Body, Support, Caption mapped to toolkit font names)
and metrics in points. Controls take colours and sizes from the theme only; a
screen that needs a colour for data (a team swatch) passes it to `swatch()` or
`TextOptions::color`. Screens reach the palette through `theme().palette`; the
legacy `FrontendTheme` style that paints the menu backdrop and panel reads the
menu theme rather than keeping its own colours, and `InGameTouchTheme::ink()`
and its siblings return the in-game theme's HUD tokens. The only literals left
are data painters (map preview frames, the end-game chart on its fixed dark plot,
team, zone and resource colours) that draw values rather than controls, and the
desktop sprite sidebar's own artwork.

There are two theme slots. `frontendTheme()` is the player's **menu theme** and
`inGameTheme()` their **in-game theme**; `themeFor(Surface)` in
`src/ui/FrontendUI.cpp` is the single table that decides which slot each kind of
surface wears: `Frontend` (menus, lobbies, settings) and `Results` (the
after-game statistics) use the menu theme; `Match` (dialogs over a match, the
HUD) and `Editor` (dialogs over the map editor) use the in-game theme.
`Glob2UI::applyThemes(menuId, gameId)` (`src/ui/ThemeCatalog.h`) assigns both
slots in place, so screens and dialogs that hold a `const Theme &` repaint in
the new colours on their next frame; caches derived from a theme (the
recoloured wordmark, the legacy style) compare `themeGeneration()`. Settings
stores the choices as `menuTheme` and `gameTheme` (defaults `light` and `dark`)
and Display → Interface appearance offers both.

#### Theme files

Themes are JSON files. Shipped themes live in `data/themes/<id>.json` and are
listed, in display order, in `data/themes/index.json`; assets they use go in
`data/themes/<id>/`. Players can add their own as `<user data>/themes/<id>.json`;
a shipped theme wins any id clash. A broken or missing file never removes the
compiled `light` and `dark` themes (`ThemeCatalog::builtinLight()` and
`builtinDark()`), and a test keeps the shipped files identical to them.

```json
{
  "schema": 1,
  "id": "ocean",
  "name": "Ocean",
  "extends": "dark",
  "palette": { "accent": "#ff8a65", "panel": "#0f2233f4" },
  "hud": { "border": "#3fa7a0" },
  "backdrop": { "kind": "colony", "veil": "#07131d70", "wordmark": "" },
  "buttons": { "kind": "palette" },
  "metrics": { "radius": 6, "focusRing": 2 }
}
```

- `id` is lowercase letters, digits and dashes; it is the preference value.
- `extends` names a theme to inherit every unset value from; without it a
  theme starts from `light`. Colours are `#rrggbb` or `#rrggbbaa`.
- `backdrop.kind` is `colony` (the live colony), `image` (`backdrop.image`,
  cropped to cover), `terrain` (the original tiled grass) or `solid`
  (`palette.backdrop` only). Missing colony or image artwork uses that theme's
  `palette.backdrop` unchanged: light paper for Light, sand for Dune, and dark
  colours for Dark and Ocean. `veil` washes over available artwork only.
  `wordmark` replaces the main
  menu wordmark with an image shown as drawn. Backdrop and wordmark overrides
  must name `.webp` artwork under `data/`; lookup retains user-directory priority.
  Without an override the shipped wordmark is
  recoloured with `ink` and `accent`.
- `buttons.kind` `sprite` paints bordered buttons with a three-slice sprite set
  (`<sprite>0..5.webp`, left, middle and right with their highlights, as
  `data/gfx/guitheme`); their labels use `ink`. Flat buttons keep the palette.
- Asset paths must stay under `data/`. Unknown tokens and invalid values are
  reported on standard error and leave the inherited value.

Every shipped theme must keep `ink` on `paper` and on fields, and `hud.ink` on
HUD panels, at WCAG AA (4.5:1), and `accentInk` on `accent` too;
`src/ui/ThemeCatalogTest.cpp` checks each one. Check a new theme in the review
gallery: `GLOB2_MENU_THEME=<id> GLOB2_GAME_THEME=<id>` select the themes for
`mobile-gallery` captures.

### Icons

Common interface icons use Tabler outlines, coloured with the active theme. Request
assets with `Glob2UI::uiIcon(UIIcon::Settings)` (or another semantic name); screens
must not load filenames. `icon(asset, {20})` creates a decorative element whose size
is in points. `ButtonOptions::icon` adds an icon before the button label with a
6-point gap; `iconSize` defaults to 20 points. The icon takes the button's resolved
ink colour, including disabled, primary and danger states.
`segments(..., enabled, icons)` and `ChoiceOptions::icons` take one icon per option:
segments pass it to each button, and a choice draws the selected option's icon before
its value and every option's icon in its popup. `MenuAction::icon` does the same for
action rows.

For an icon-only button, pass empty visible text and a translated
`accessibleLabel`; unnamed icon-only buttons are rejected. Set `tooltip` to the
translated label. The host displays it after 600 ms of pointer hover or keyboard
focus, keeps it inside the safe viewport, and dismisses it on activation or target
change. Touch actions retain their normal minimum target size. A missing asset
restores the accessible label as visible button text; missing raster files are
logged when the asset is first requested. These names support diagnostics and
harnesses; they do not establish native screen-reader integration.

`GAGGUI::ui::IconAsset` owns white alpha-mask raster variants sorted by width. The
canvas chooses the smallest adequate resolution and caches recolours, preserving
coverage without changing the source mask. Recording canvases record `drawIcon`
identity, bounds and colour without accessing pixels. Game-side lookup uses a weak
cache keyed by asset name: live elements own their masks and recolours, aliases
share them, and closing the last element releases the graphics resources.

The main-menu Settings button uses a gear with its label in the desktop utility
grid, and a gear alone beside the wordmark on touch hosts. Desktop Editor and Load
game also retain their labels beside icons. Touch main-menu launch choices and
More-page actions all use icons beside labels, including Back and More. Desktop
settings-sidebar categories and the compact category dropdown share the same
labelled icons. Game-specific artwork retains its existing presentation.

Choose icon-only controls for familiar actions in headers and toolbars. Keep
labels in menu rows and desktop utility grids so neighbouring controls read
consistently. Mobile interfaces can use icons more extensively to aid scanning,
while retaining labels for destinations that need explanation.

`Glob2UI::compactButton` applies that toolbar convention: translated text on
pointer hosts, a 24-point icon in a button at least 48 points square on touch
hosts, with a diagnostic name, tooltip and visible-text fallback. Mobile chat
uses Send and Close icons, the compact landscape picker uses a Back arrow, and
custom-game previews use Refresh and Info icons for reroll and start quality.
The custom-game lobby labels its tabs, footer actions, map-mode pills, parameter
buttons, section headers (with a chevron for open or closed), team formats, colony
controllers, rule groups and rule views with icons beside their text; rule values
stay text-only.
Launch/confirmation choices, parameter operations and immediate file deletion
keep their labels. Landscape parameter fields widen with mobile text size so
numeric choices remain readable. The gameplay action strip retains its
game-specific sprites and labels, which distinguish construction, flags and team tools.

Original SVGs and a source manifest live in `datasrc/icons/tabler/`, pinned to
Tabler v3.48.0. Generated PNG sources live in `data/gui/`; the distributed MIT notice is
`data/tabler-icons-license.txt`. Ordinary builds convert the committed sources to
WebP in the runtime tree and need no SVG renderer or network access. To regenerate with the pinned development tool:

```sh
npm install --prefix artifacts/tabler/tooling --no-audit --no-fund @resvg/resvg-js@2.6.2
NODE_PATH=artifacts/tabler/tooling/node_modules node tools/icons/export_tabler.cjs
```

The exporter verifies source hashes and writes 20- and 24-point icons at 1×, 2×
and 3×. Add new assets deliberately to the manifest and semantic bindings, retain
upstream notices, and capture desktop and phone views when introducing them.

The online web app (`platform/apps/web/src/icons.tsx`) imports the same SVGs for its
navigation and other interface glyphs, so the game and the website share one icon set.
A web-only icon still goes through this manifest; do not add a separate web icon
library. Game sprites remain the web app's decorative art (`platform/apps/web/art/README.md`).

### Hosting a dialog

```cpp
std::unique_ptr<Glob2UI::InGameDialog> dialog = std::make_unique<InGameOptionScreen>(...);
dialog->attach(*globalContainer->gfx);          // once, on open
// each frame
if (dialog->event(event)) return;               // consumed
dialog->update(SDL_GetTicks());
dialog->draw(SDL_GetTicks());
if (dialog->finished()) act(dialog->result());
```

Dialogs override `onEscape()`, `available()`/`place()` for non-centered
placement (the chat composer sits at the bottom), `maxWidth()`, `fillHeight()`
and `scrim()`. `resume()` reopens a dialog whose result was consumed but must
retry (a failed save).

A dialog can also be a permanent docked panel. The map editor's brush browser
(`src/map/editor/EditorDock.h`) is an `InGameDialog` that never finishes and has
no scrim: `available()` returns a full-height column at the right edge
(`clamp(300pt, 240, 40%)` wide, so it follows the interface scale), `place()`
fills it and `paintPanel()` paints the column. Its owner draws it beneath any
modal dialog, sends it pointer events inside its rectangle or while
`host().interacting()`, and keys only while a text field is editing; strokes that
started on the map keep their events. `onUpdate()` compares the model (catalogue
revision, selection, panel mode) with what the last `build()` saw and invalidates
on change. Its controls publish under `dock/...` and `brush/<catalogue id>`.

### Focus, keyboard and touch

The host keeps one focus ring over interactive elements in tree order. Tab and
Shift+Tab move focus, arrows move within lists, sliders, segments and popups,
Enter and Space activate, Escape ends text editing, closes a popup or fires the
screen's Escape route. Shortcut keys on buttons fire without focus. Touch
input distinguishes taps from pans; a pan scrolls the innermost `scroll` and a
release outside the pressed element cancels it. Pointer capture guarantees
press and release land on the same element.

Finger pans on `scroll`, `listView` and `textEditor` (the nodes whose
`inertial()` is true) have the physics of a native list, driven by
`GAGCore::ScrollAxis` from `libgag/include/ScrollPhysics.h`:

- the content tracks the finger, and after release coasts with the finger's
  velocity (a least-squares fit over the last 100 ms; a finger that rests for
  40 ms before lifting has none) and decelerates at the iOS rate;
- dragging past either end stretches the content with the iOS rubber band and
  releasing it springs back with a critically damped spring; a coast that
  reaches an end hands its velocity to the same spring;
- a touch on coasting content stops it where it is and is not a tap; the
  wheel, a scrollbar press, `scrollIntoView`, `scrollToEnd`, a rebuild that
  moves the content, focus loss and a resize all end the motion.

Nodes keep an `int` offset that is always clamped, which is what
`NodeState::scroll` persists, plus a transient `overscroll()` displacement that
is painted and hit-tested but never saved; `scrollTo()` sets the clamped offset
and `setOverscroll()` the stretch. Mouse drags and conventional wheel events never coast or stretch. `Host::animating()` reports a coast or bounce
in progress and `UIScreen::executionDelay` frames every 16 ms meanwhile.
The three preference sliders under Settings › Controls (and the in-game
Options dialog on touch) tune the feel through `GAGCore::scrollTuning()`; 0
turns momentum or bounce off, 50 is the researched default.

On macOS, phased trackpad and gesture-mouse scroll events use native momentum
and a gentler rubber band (half the touch coefficient). The AppKit local monitor
in libgag intercepts only precise events with gesture or momentum phases and
queues a value-only `GestureScrollEvent`; ordinary wheels remain SDL wheel events.
Devices with momentum phases but no direct-contact phases also retain the wheel
path throughout, since they cannot establish gesture capture.
`GestureScrollController` feeds those deltas into the existing axis without adding
another fling. Native point deltas are converted into logical coordinates once.

The deepest scrollable at gesture start owns the whole sequence, including its
momentum after the pointer leaves. Stable-key rebuilds retain that capture; removal,
modal changes, focus loss, viewport changes and external scroll jumps cancel it
and consume the remaining tail. Active Mac gestures request 16 ms frames during
contact and the short wait for native momentum. Content stretches while fingers pull past an edge;
release or momentum reaching an edge starts the spring and stops further tail
movement. A 150 ms gap after release or during momentum closes an incomplete
sequence; a resting finger never times out. Horizontal editor trays choose the
initial dominant axis and accept vertical input as an alternative.

The same controller drives shared widgets, HUD palettes/actions/tutorials and
editor trays/inspectors in their existing presentations. Unclaimed map gestures
retain wheel zoom. List momentum at zero suppresses native UI momentum; positive
values leave its duration and speed to macOS. List bounce adjusts the gentler
edge stretch, and zero bounce or reduced motion gives firm bounds. No extra
preference fields or simulation state are introduced.

## Verification

- `scons unit-tests` then `python3 test/run_tests.py --binary unit --filter 'UILayout/*'`
  runs `libgag/src/ui/UILayoutHarness.cpp`: pure layout with fixed-advance text and a
  recording canvas. Measure and arrange,
  adaptive re-choice after resize, footer folding, scroll clamping, wrapping,
  ellipsis, focus order, capture, tap versus pan, fling, overscroll and bounce
  (with an explicit clock: event timestamps and `host.update(tick)`), popup
  routing, per-key state across rebuilds and text editing. The `ScrollPhysics`
  suite covers the kernel itself. `GestureScroll` covers native delta sequences,
  fractions, axis selection, bounds, settings, cancellation, timeout and deferred
  SDL payload copies; the Mac-only `MacScrollMonitor` engine suite checks native
  phase/delta conversion, wheel pass-through and monitor teardown. Physical
  trackpad and gesture-mouse feel still require hands-on verification.
- The `UIPresentation` suite in `glob2-engine-tests`
  (`python3 test/run_tests.py --filter 'UIPresentation/*'`) instantiates every
  screen and dialog fixture at phone, tablet and desktop viewports in both touch
  and pointer presentations, with and without platform gutters, and requires:
  every interactive element inside the safe rectangle, touch targets at least
  the theme minimum, no overlapping interactive elements, unique keys, something
  focusable, and a valid focus and scroll state after resize. It writes one
  image per screen and viewport into its artifact directory.
  `GLOB2_UI_ONLY=<fixture>` restricts a run. `GLOB2_UI_VIEWPORT=<viewport>`
  selects a named viewport, and `GLOB2_UI_REVEAL=<control-key>` scrolls a control
  into view before capture and layout verification.
- The `GameGUITouch`, `Settings`, `EngineSession`, `GameSpeed`,
  `CustomGameSetup` and `MapPreview` suites, `menu-colony-harness` and
  `mobile-gallery` drive the ported screens through their keys and semantic
  entry points; run them after changing a shared builder or the host.
- `GLOB2_UI_DEBUG=1` draws element bounds and keys over any screen.
- Feel is not covered by tests: resize the desktop window live through the
  menus, rotate with `GLOB2_MOBILE_UI=1`, and play a session before merging a
  change to a shared builder or theme.

## Adding a screen

1. Derive from `Glob2UI::Screen` (menu), `Glob2UI::Dialog` (frontend modal)
   or `Glob2UI::InGameDialog` (gameplay/editor modal).
2. Keep the model as plain members; write `build()` with the containers above;
   give each control a key and a callback that changes the model and
   invalidates.
3. Add the screen to the presentation harness fixtures in
   `src/ui/UIPresentationHarness.cpp` and, for a reviewable capture, to
   `tools/MobileGalleryHarness.cpp` and `tools/mobile_gallery/catalog.json`.
4. Expose semantic entry points for interaction tests instead of coordinates.

## Typed inspection and semantic actions

`ui/Inspection.h` defines transport-independent `SurfaceSnapshot`, `ControlSnapshot`,
`ControlValue` and `SemanticAction` types. `Host::inspect()` describes the tree and
popup, including clipped bounds, roles, labels, values, selection, focus and supported
actions. Coordinates are logical pixels. A popup blocks its underlying tree. The
screen receiving input supplies `Screen::inspectionHosts()`; modal owners expose
only the active dialog. Native file pickers are outside this interface.

`Host::act(key, action, error)` validates visibility, enablement and popup ownership.
Standard buttons/toggles activate, choices/lists select an integer index, numeric
controls set an integer value, text controls set a string, and scrollable nodes scroll
by a signed logical-pixel delta. Draft text fields retain their usual Enter/blur commit
and Escape cancellation. Password values remain masked. Custom nodes extend
`controlValue()`, `supportedActions()` and `perform()` alongside ordinary input.
Callbacks must remain application-thread work.

Surface instances are lifetime identities. Each keyed control has a monotonic target
revision that changes when its own description, geometry, value or availability changes,
or when it disappears and returns. Focus and unrelated children do not change that
control's revision. Surface revisions include changes to the hierarchy. The browser's
flattened diagnostic JSON comes from `diagnosticJSON()` over this same snapshot.

`SemanticDispatch` carries an owned command through ordinary additional input using
an opaque event ID. The host checks the instance and target revision when it consumes
the event. Retired commands cannot leave borrowed pointers in deferred event queues.
Screen owners must route semantic events to the same dialog/host that owns human
input. Returning a host from inspection without routing its events is incomplete.

Custom-rendered controls use `ui::PositionedControl` and `positionedControls`: the
domain model supplies logical bounds, clipping, state, supported actions and shared
callbacks before painting or hit testing. Paint and human input consume the same
geometry; inspection is not a second coordinate map. Build a fresh model at action
consumption, and read gameplay presentation from the published Scene. A paint scope
may retain one immutable model for the duration of a draw to avoid rebuilding every
control's callbacks for every field.

Compound building edits read the same pending presentation values for human and
semantic input. An execution receipt does not imply that the displayed Scene already
contains its result: retain the pending field until `executedOrderRevision` reaches
that receipt. Reconcile only fields with pending values, and retire their receipts
on building removal or session initialization.

Stable keys alone do not distinguish a reused entity slot or a changed construction
preview. Override `Node::semanticIdentity()` or wrap a subtree with `semanticContext`
using the domain's object generation and relevant action context. That identity is
inherited by descendants and participates in their revisions without being exposed
as transport data. Include context that changes the meaning of a callback; do not
include frame ticks or unrelated counters. Host assigns popup lifetimes automatically,
so closing and reopening an identical popup invalidates its earlier targets. Surface
owners must enforce popup/modal ownership across separate hosts at consumption as
well as inspection; an inspectable covered control must never receive input.
Choices bind their full catalog identity and enabled state. Owned popups also bind
their opener's semantic context; changing or retiring the opener closes the popup
before a stale selection can reach a replacement callback.

Surface owners can set `Host::setBlockedRegions` from higher surfaces' visible
panels. Blocks affect inspection, semantic dispatch, pointer acquisition and keyboard
actions. Rectangular occlusion conservatively blocks an entire target on any visible
overlap; shaped or partly covered controls can require dismissing the covering panel.
Uncovered sibling rows remain actionable. Changing a blocker cancels only newly
covered gestures, preserving unrelated presses and scrolling. Suspended text drafts
are retained, and Tab can move focus to uncovered controls. Legacy compact keyboard
focus is mirrored into the Host so typed focused state and actual keyboard targeting
agree.

### In-game statistics

The Statistics view shares visible topic buttons and a scrollable metric list
between the 200-pixel desktop sidebar and the mobile bottom sheet.
The desktop minimap stays 128 pixels wide and is centered within the sidebar.
Overview highlights population, growth, hunger, health, worker idleness, blocked
buildings, warriors and intruders. Colony contains Population and Food; Economy
contains Work, Materials and Buildings; Military contains Military; World contains
Map and Score. Colony also retains the snapshot-only Unit details. Live spectators
retain their existing win-chance display under World.

Selecting a metric shows its current or watched team's history below that group.
Labels and values use separate aligned columns; unavailable readings use a dash
and one shared explanation. Larger text uses fewer topic buttons per row and
puts readings below full-width labels. Open full history expands the chart into an in-game
dialog while the match continues. Its Details disclosure explains the metric;
short landscape layouts put the chart before its controls. Rate/total and averaging controls use the metric
catalog's capabilities and defaults. Topic, per-topic scrolling and per-metric
history choices last for the current match; they are not saved as preferences.
Closing history restores focus to its metric. The former graph view is now Map
overlays, retaining its tab position and the four overlay toggles.

`GameGUI::statisticsHost()` exposes the panel's embedded Host for painting, human
input, typed inspection and semantic dispatch. `statisticsBounds()` supplies shared
desktop and compact geometry. Team and viewing context participate in target
revisions, so a command cannot act on a replacement watched team.

`InGameStatisticsPanel` holds immutable statistics/history leases rather than
live simulation pointers. Visible statistics and history request the history
component explicitly; hidden views release it. Waiting for publication, insufficient
history and statistics not recorded in older saves have distinct messages, while
recorded zeros remain numeric. The mobile sheet header handles downward dismissal;
the embedded framework host handles scrolling so a content drag cannot dismiss it.
