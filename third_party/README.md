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
security, numeric, continuation and cross-platform suites.

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
