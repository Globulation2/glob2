# Share soundtrack packages

For composition, assembly and QA, use the [music pipeline](music-pipeline.md) and [style guide](music-style-guide.md).

## Community releases

The online **Music** catalogue accepts three externally composed arrangements:
Calm, Building and Combat. The workflow is upload → inspect → choose repairs →
convert → audition → publish. Registered creators publish after technical
validation; musical advice does not require moderator approval. A revision is a
new release, with its own likes. Community licences are CC0 1.0, CC BY 4.0 and
CC BY-SA 4.0, with credits, source links and an explicit AI disclosure.

`glob2music.community` decodes supported direct media containers with FFmpeg,
resamples to 48 kHz stereo, and compares exact frame counts. WAV, FLAC, MP3,
AAC/M4A, Vorbis and Opus are supported when their decoders are available. Playlist,
concat, network and ambiguous multistream inputs are rejected. Each mood must be
10–900 seconds; the shipped soundtrack's 50–120 seconds remains guidance.
Unequal lengths require an explicit end trim to the shortest or silence padding
to the longest. Neither operation aligns beats or harmony. Optional mastering
uses the existing −18/−17/−16 LUFS ladder and peak limiter. Original files must be
uploaded again to make different processing choices after conversion. Temporary
inputs survive transient storage or database failures for up to three worker
attempts. Technical rejection, exhausted retries, cancellation and successful
conversion delete them after recording the terminal state; abandoned drafts
expire after 24 hours without activity.

The loop-aware encoder in `tools/encode_music.py` produces Ogg Opus at 48 kbps
stereo VBR. Tagging replaces the comment packet and recomputes Ogg page sequence
numbers and CRCs without changing audio packets, pre-skip or end granules. Every
final tagged file is fully decoded again to check frame counts. Waveform peaks,
loop-seam and decoded-peak advice accompany the exact downloadable preview.
Cover images are re-encoded as 512 × 512 JPEGs, at most 256 KiB; an omitted cover
uses a deterministic terrain illustration seeded by its release identity.

### Portable file contract, version 1

A release is one directory containing only `a1.opus`, `a2.opus`, `a3.opus`.
Bulk ZIPs repeat this structure with a UUID directory per release. Calm (`a1`) is
the authoritative source of display metadata; every track repeats identifying
metadata. Installation needs neither a sidecar nor an online lookup.

| Comment | Meaning |
| --- | --- |
| `TITLE`, `ALBUM`, `ARTIST` | Mood track title, set title, artist |
| `DESCRIPTION`, `LICENSE`, `COPYRIGHT`, `SOURCE`, `GENRE` | Description, licence identifier, attribution, newline-separated source links, tags |
| `GLOB2_SCHEMA` | `1` |
| `GLOB2_RELEASE`, `GLOB2_ORIGIN` | Release UUID and originating instance |
| `GLOB2_MOOD` | `calm`, `building`, or `combat` |
| `GLOB2_FRAMES` | Exact decoded stereo frame count at 48 kHz |
| `GLOB2_AI_GENERATED` | `0` or `1` |
| `METADATA_BLOCK_PICTURE` | Optional base64 FLAC picture block containing JPEG bytes |

Final tagged bytes are SHA-256 hashed. Imports require three distinct, correctly
assigned moods, matching identities/origins and lengths, one stereo logical
stream, bounded metadata, and a successful full decode. See the
[Opus metadata API](https://opus-codec.org/docs/opusfile_api-0.12/group__header__info.html)
and [Ogg Opus specification](https://www.rfc-editor.org/rfc/rfc7845.html).

### Streaming playback and checks

`src/audio/MusicStream` provides the native and website preview mixer. Three
bounded 1024-frame stereo buffers advance from one clock; compressed files are
retained instead of whole-song PCM. The default fade has the game's smoothstep
curve and approximately 0.37-second duration. Preview-only controls add seeking,
a 0–10 second test fade, continuous blend and an eight-second mood sequence.
Gameplay uses the same decoder read/seek helpers and computes the original fade
curve instead of retaining a lookup table. Preview suspends background music and
restores it when its screen closes.

The website player allows mood selection and seeking before its first explicit
Play action. A shared transport pairs three waveform lanes with one accessible
playhead, volume and mute. Guided game-transition previews cycle moods every
eight seconds of playback using game fade timing; custom fades, manual blends
and percentage weights live under **Advanced mixing**. Selecting a mood or
changing a custom setting exits guided preview. Pausing or hiding the page stops
the sequence; returning to the page does not restart audio.

Song pages summarize **Audio quality checks** with readable listening guidance
for warnings and failures, and keep passing results and measurements under
**Show technical results**. The studio summarizes the latest candidate per check
category and retains all attempts, previews and reports in **Generation history**.
Missing, skipped, waived and incomplete results are never counted as passes.
Completed versions promote listening, revision, downloads and explicit
publication. Comparing versions retains mood and volume; playback position is
retained only when both musical timeline identities and frame counts match.
A new delivery follows automatically only while **Following latest generation**
is selected; manually selected history is not interrupted.

`python3 tools/music/build_web.py` builds the website decoder with the pinned
Opus dependencies and Emscripten SDK. The website owns the decoder in a worker,
keeps at most eight PCM blocks queued, and feeds an AudioWorklet; resampling
happens after synchronized mixing. Native preview feeds SDL's output conversion.

Focused regression commands (FFmpeg and native Opus development headers required):

```sh
PYTHONPATH=tools/music python3 -m unittest tools.music.tests.test_community tools.music.tests.test_runtime
scons release=1 engine-tests
python3 test/run_tests.py --help
```

The `CommunityMusic` suite checks the legacy fade curve and input boundaries;
`CommunityMusicUI` captures the dedicated screens. `test_runtime.py` exercises
actual processor output through the C++ importer and player, including seek,
loop boundaries and rapid switching. The website's `e2e/music.spec.ts` uses the
same generated audio with the real WASM decoder. To exercise the maximum duration,
use `make_community_fixture.py <output> --seconds 900` and the runtime probe.

The browser-game import check uses a real catalogue download, passed as
`GLOB2_MUSIC_TEST_ARCHIVE`, with `browser/tests/music-import.spec.js`. It drives
Audio settings, the file picker, installation and selection, then reloads the
browser to verify the persisted bytes. Its quota-failure case verifies recovery
export and a successful retry. Without that archive, the integration cases skip
explicitly; the platform web E2E suite can generate one.

## Online AI Music Studio

The optional online Music Studio uses the symbolic layer on CPUs. Its versioned
`acoustic-v1` palette contains the instruments pinned by the four approved acoustic
sets; `synth-v1` contains Glass Garden's warm Surge XT patches and DSP voices. It
uses neither ACE-Step nor uploaded recordings. Assets are provisioned at image
build time by `python -m glob2music.studio.install_assets CACHE`.

The composition agent writes `composition.py` with `SCORE`, `arrange(mood)` and
optional `MIX_ADJUST`. Generated code runs only in an isolated export process. A
bounded JSON score crosses into a fresh trusted process, which checks ranges and
structure, humanises, renders, folds tails, mixes, masters and encodes. Generated
performance hooks and QA waivers cannot cross this boundary. Part names and
instrument identifiers are validated before they can become filesystem paths.

All ten audio checks run against the final encoded files, including the license,
credits and AI-disclosure tags. Missing checks, skipped measurements and failures
prevent delivery; warnings remain visible. Candidate evidence and composition
sources stay private. Revisions edit the selected version's saved source. One
request includes at most three render attempts within its model and CPU budgets.
An attempt is reserved durably before execution; an interrupted render consumes
that attempt. Completed candidate bytes are checkpointed in private blob storage
and reused after recovery without rerendering or repeating a provider call.
Python and NumPy's global random generators are seeded before recipe import and
arrangement; compositions should use those generators or explicitly seed their
own generators from the requested seed. Pipeline identity includes both the
Python audio modules and the worker's prompt, tools and isolation implementation.
These automated checks do not replace listening; the service and each palette
need listening and cost qualification before operators enable credit sales.

Choose the release license before generation, then confirm it when publishing.
The trusted encoder embeds the license, AI disclosure and pipeline/sample credits
before final QA. Download ZIPs keep the native importer's three-file layout
(`a1.opus`, `a2.opus`, `a3.opus`); attribution travels in the Opus tags.
A timeline fingerprint covers tempo, meter, harmonic form and section boundaries;
the comparison player carries its position only between matching timelines and
frame counts.

Related: [asset production](README.md).
