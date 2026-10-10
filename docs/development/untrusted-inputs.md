# Loading untrusted files

Defensive decoding and bounded resource use for maps, saves, replays and script state.

## Untrusted maps, saved games and replays

Treat serialized fields as untrusted before using them as indices, allocation
sizes or runtime state. Map/save game and GUI decoding use checked binary reads;
a short field raises an error instead of supplying partial or zero-filled state.
Keep these scopes around the entire load, including legacy fields. Callers must
handle both a false result and a decoding exception, and must not run a partially
loaded game. Map headers validate their team records, and entity loaders validate
types, identities, levels and cross-references before using them. Map sector
dimensions must match the dimensions derived from the terrain grid.

Names from headers are converted to a single portable filename component before
download/save paths are built: separators, controls (including NUL), Windows
reserved characters and device basenames cannot redirect writes outside the
selected directory.

Gzip file loaders limit compressed input to 256 MiB and expanded output to 2 GiB.
The separate expansion limit permits the existing late-game snapshots (roughly
1 GiB uncompressed) while still bounding hostile compressed streams.
Scenario objectives, hints and legacy areas are limited to 65,536 records;
building/unit reference lists are bounded by the corresponding entity capacity.
AI and history collection reads are limited to 1,048,576 entries per collection,
with Maxima continuation retaining its explicit 16,777,216-entry bound for
large saved geometry tables. Nested AI condition graphs are limited to 64
factory calls. Unknown AI implementations,
object tags, invalid module indices and malformed queued orders are rejected.
These are reader limits, not a new disk format. Files exceeding these limits are
rejected. Ordinary valid saves retain their format versions and continuation
semantics.

Replays must contain a complete, valid order stream ending in a null order.
Truncated recordings are rejected, including recordings with a valid prefix;
playback no longer tries to recover a prefix from a malformed stream. Both scanning
and playback check short reads, and the scan rejects overflowing step totals.
Execution checks player/team references before indexing game state. This is not
an authentication mechanism for multiplayer commands. Voice orders additionally
limit encoded audio to 4,096 bytes and 121 frames before copying or decoding.
The recorder flushes after crossing 2,048 bytes or 120 frames, so the receive
limits retain headroom for its final frame and existing recordings.

Embedded legacy USL has no `load` file capability. The application still loads its
own runtime libraries through the host API. Native type errors stop the map script
instead of asserting or dereferencing an invalid value. USL also rejects integer
overflow, limits source to 1 MiB and 65,536 tokens, bounds parser nesting (128)
and expression chains (256), and checks runtime frames (1,024), frame operands
(65,536) and heap values (262,144) at instruction boundaries. Garbage marking
uses an iterative traversal to avoid overflowing the native stack. Legacy SGSL saved program
counters must match parser-derived statement boundaries or the integer suspension
point of `wait(N)`.

These checks do not establish a hostile-script sandbox. Script interpreters and
native simulation code still share the game process. Per-collection and
instruction-boundary limits are not an aggregate byte or execution-time budget.
Keep dependency review, sanitizer fuzzing of the complete load-and-step path,
and platform replay/checksum comparisons separate from targeted rejection tests.
See [JavaScript scripting](../scripting/javascript.md) for that interpreter's capability and
resource boundaries.
