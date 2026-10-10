# Terrain resource sets

Companion to [resource catalogs](resource-catalogs.md).

## Themed terrain and resource sets

The online **Terrain & resources** library at `/sets` holds flexible sets of custom
terrain, resource deposits, or both. A release includes definitions, PNG
spritesheets, frame grids and mappings, and attribution under CC0-1.0 or
CC-BY-4.0. Sets reuse the engine's existing material identities; they do not add
new inventory materials. Presets supply initial properties, and the web workspace
exposes property overrides, material yields, stock levels, variants and animation.
Advanced terrain colors, seams and decor use the engine's material declaration.

### Authoring and sharing a set

1. Open **Terrain & resources → Create a set**, enter its title, tags, license and
   creator credit, then add terrains or resources from presets. Each entry keeps a
   stable key within a release; changing its display name does not break updates.
2. Set the PNG frame width/height before **Upload PNG**. Terrain frames must be
   32×32; resource/decor frames can be up to 64×64. Select the uploaded image in
   the entry's **Spritesheet** control. Reupload identical image bytes with new
   dimensions to repair a mistaken frame grid, then check every frame mapping.
   **Remove sheet** removes unused images; change or remove entries that still
   reference an image first. Identical PNG bytes must use the same frame grid
   when combining sets in one map.
3. Click a tile in **Inspect sheet**, or type **Selected frame**, to choose a frame.
   This only selects a candidate: **Add selected frame** assigns it as a variant.
   Variant weights choose relative frequency; animation frame count, stride and
   ticks control the frame sequence. Resource stock levels select variants by the
   total stock across all materials.
4. Edit supported gameplay properties and material yields. Terrain Q8 fields use
   fixed-point units: 256 = 1; health fields express signed HP per exposed tick
   divided by 256. Resource rates use 196608 for one opportunity/probability one,
   with bounds described [in the field reference](resource-properties.md#simulation-properties). Advanced JSON
   edits remain attached to their entry when switching entries; correct every
   invalid edit before saving.
5. **Save draft** keeps a private working copy. **Preview current changes** renders
   unsaved content locally in the browser. **Run checks & preview** saves and
   validates the exact revision for publication; a later edit requires new checks.
   Checks need an available set-validation engine agent on the instance.
6. Enter a release label, notes and visibility, verify credits/reuse rights, then
   **Publish this release**. Use **Create new release** for later revisions.
   **My sets → Load more drafts** reveals older working drafts. On a public set,
   **Inspect latest release** shows read-only entry properties before importing.
7. In the map editor, open **Set Library**, inspect an exact release, select its
   entries and import them, or download its package and import it from disk.
   Painting and local edits use the map's copied content. To update placed entries,
   explicitly choose **Replace placed entries** for another release; review the
   warning about replacing local edits and capping existing stocks.

The native CLI can check a downloaded or authored package without an online
instance:

```sh
glob2 map validate-set package.json --report-file report.json --preview preview.png
```

The report binds the exact package hash to its validation result; the optional
preview renders a contact sheet of up to 64 entries. An invalid package produces
`valid: false` with a reason and a successful command exit; invocation, input-file
and report-output errors produce a nonzero exit. Inspect the report's `valid`
field before treating a package as usable.

Drafts are private and saved with revision checks. Checks bind the exact serialized
package hash to an engine validation job. Saving again invalidates those checks.
Publishing creates an immutable release; further editing creates a new release.
Public sets appear in the library, unlisted sets are accessible by link, and private
sets are visible to their owner. Withdrawal or moderation blocks future downloads,
without changing copies already incorporated in maps.

The map editor's **Set Library** searches the current online instance and imports a
chosen release or selected entries. Required custom resources are included with
selected terrain. The **Import Terrain & Resource Set** menu action accepts a
bounded `.json` package from disk, including in the browser editor. Entries use
stable keys derived from their set and release UUIDs, so independently authored
sets and releases can coexist without colliding.

Each map retains its own editable copy. The library dialog's **Map content, credits
& local edits** panel edits the copied definitions and terrain material declaration,
with the same validation applied before publication to the map. Original credits
remain. Reimporting existing entries cannot silently overwrite those edits. To
update placed content, choose another release of the same set and explicitly select
**Replace placed entries**. Matching entry keys within the set move to the new release;
removed entries remain on the old release. Local changes to replaced entries are
superseded, and material stocks are retained up to the new capacities.

Format 144 stores a map-owned artwork bundle alongside the existing terrain and
resource snapshots. Only custom artwork and set attribution are bundled. Built-in
terrain and resource graphics continue to come from the installed game. Loading,
playing, sharing and replaying a map require no library lookup, creator account or
previously installed set. Scene snapshots retain the immutable bundle; graphics
objects are created and destroyed on the render thread.

Packages are limited to 16 MiB, 256 sheets, 2048×2048 pixels per sheet and 64 MiB of
decoded pixels in the combined map bundle. Terrain frames are 32×32; resources and
decor can use frames up to 64×64. Hashes, PNG decoding, duplicate JSON keys, depth,
paths, property bounds and effective animation/decor frame indices are checked.
Invalid imports leave the map's catalogs and bundle unchanged. Existing saves remain
readable at the durable compatibility floor; maps with new bundles require format
144. Format 149 also stores pending resource-growth work alongside vertex terrain
and scheduled building gradients; the durable save compatibility floor is unchanged.

### AI Terrain Studio

**Create with AI** opens `/terrain-studio`. Describe the terrain, resource deposits,
visual theme, and gameplay you want. A clear creation or revision request starts
one build affecting up to twelve entries; questions and brainstorming remain
conversational. An available terrain credit is required for discussion. Each
validated delivery costs one credit, including property-only revisions; confirmed
failures return the reserved credit. Publishing is a separate action.

Start fresh, choose **Edit with AI** on an owned unpublished draft, or choose
**Remix with AI** on a released set. Remixes preserve source credits and licenses.
Upload up to four selected PNG, JPEG, or WebP references to guide appearance;
references and generation artifacts stay private to the project owner. Use
artwork you have permission to reference.

The designer uses existing terrain capabilities and inventory materials. It can
change movement, construction, hazards, ecology, growth, clearing, and material
yields, but cannot invent engine mechanics or inventory materials. Ground gets
four texture variants; generated resources get three stock stages and two variants
per stage. Requested animation uses a gentle glow pulse, not articulated movement.
The default style follows the game's painterly artwork; explicit alternate styles
are supported subject to readability and technical asset limits.

The scene gallery uses the game compositor for isolated cells, narrow paths,
mixed boundaries, raised decor, resource stock stages, and selected animation
phases. Terrain variation selects another deterministic visual seed; resource
variation selects a frame within the stock level. Review the properties alongside
the artwork: passing import checks does not establish balance for every map.
The equivalent native preview is:

```sh
glob2 map validate-set package.json --report-file report.json --preview gallery.png \
  --gallery 1 --phase 0 --variation 0
```

Deliveries update the ordinary private set draft. Manual controls remain available
in the studio inspector and in the set workspace. Save manual edits before sending
a new AI request. If another client changes or publishes the draft during a build,
the validated result becomes a saved candidate; adopting it explicitly replaces
the current draft content. Candidates can also be downloaded. Complete normal set
publication to share a release or import the downloaded package into the map editor.

Related: [features and content](README.md).
