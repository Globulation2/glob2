# Vendored scripting dependencies

QuickJS-NG v0.17.0 (MIT), from https://github.com/quickjs-ng/quickjs/releases/tag/v0.17.0.
OpenLibm v0.8.8, from https://github.com/JuliaMath/openlibm/releases/tag/v0.8.8.
Retained license notices accompany each dependency. Only engine sources and the
portable double-precision math subset are retained; command-line and OS bindings
are excluded.

Local changes namespace OpenLibm public math symbols, redirect QuickJS numeric
operations to that subset, meter interpreter dispatch, and reject dynamic
Function compilation and regular-expression literals. Build both libraries with
strict floating-point flags. Upstream updates require rerunning the scripting
capability/resource, numeric, continuation and cross-platform suites.

OpenLibm compatibility aliases are removed so no system math symbol is exported.
QuickJS also uses a fixed context hash seed, a deterministic 256-frame recursion
limit, and lexical rejection of BigInt and async/await. Native operations charge
linearly for receiver/argument container sizes before execution. Profile restrictions
are host/runtime policy; never link quickjs-libc or expose the CLI's OS modules.
The host installs only base, JSON and Map/Set intrinsics (Promise/Eval internals
support module evaluation but their user globals and constructors are disabled).

JSON numeric parsing uses QuickJS's own dtoa parser instead of host strtod.
Native allocation and physical-stack failures set a sticky runtime flag checked
by the host even when a script catches the JavaScript exception.

Additional native metering covers actual array-like lengths, JSON tokens and
recursive serialization, recursive flattening, and generated string repeat/pad
lengths. These hooks prevent shared graphs, accessor-provided lengths and
generated output from bypassing the interpreter work budget.

String concatenation, comparisons and dynamic property-key conversion charge
logical string lengths, including rope strings. Tests retain adversarial cases
for these operations as well as exponentially shared JSON/array graphs.

Logical 64-call guards cover recursive parser entry points, JSON traversal and
array flattening; physical stack checks remain a fatal fallback. Container/string gateway
charges are linear. Actual coerced string search lengths, default sort string
comparisons and emitted string-buffer characters are metered within operations.

String/number coercion charges actual string lengths after object conversion.
Native search, sorting and output charges remain in the operations themselves.

`javascript-vendor.json` records the exact upstream commits, tag-archive SHA256s,
retained files and ordered patch series. The first patch for each dependency
reproduces the original profile fork; later patches record the engineering pass
and host-only module-binding access for automatic global persistence. No heap
pointers or executable bytecode are serialized.
Run `python3 tools/javascript/verify-vendor.py` to download the pinned archives,
verify their hashes, reconstruct retained sources in temporary storage and reject
unexplained differences. `--archives /path/to/archives` uses previously downloaded
`quickjs-ng.tar.gz` and `openlibm.tar.gz`. The command never edits the checkout.

OpenLibm `hypot` is retained and namespaced alongside the other double routines.
Half-number conversion and clamped integer rounding also avoid system math.
The build checks interpreter, math and host conversion objects for unexpected
platform numeric symbols. The operation inventory and trusted-code assumptions
are maintained in [the scripting guide](../docs/development/javascript.md).
