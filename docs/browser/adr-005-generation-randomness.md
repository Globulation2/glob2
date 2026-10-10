# ADR 005: generation ownership and scheduling

Status: accepted.

The browser uses the shared `GenerationService`, registered generators and
`GenerationRequest` options. It preserves the current lobby layout, landscape
picker, start-quality scoring, candidate selection and generated-map snapshot.

`GenerationContext` derives named random streams from the request seed. `GenerationService` initializes the target map’s world streams from that seed; generation does not borrow another game’s synchronized RNG.
Native and threaded browser landscape previews use the same worker threads. The serial browser fallback
calls `LandscapePreviewer::poll()` to complete one queued generation attempt;
the serial WebAssembly runtime starts no worker threads. The lobby and landscape picker both
service this queue. Failed attempts are requeued up to the same three-attempt
budget. The picker starts work after layout, orders pending maps by distance
from the viewport center, and only advances cards intersecting the viewport. It
spaces attempts apart to leave time for rendering and input; unseen maps stay
queued until browsed. Interaction postpones the next picker attempt. Only a
completed candidate can become a launch snapshot.

Landscape and start-quality dialogs are owned children of `ScreenStack`. The
selected request and seed are copied back before the child is destroyed. A
launched custom game's snapshot owner is transferred to `GameLoadScreen`, so
closing the lobby cannot delete a map that has not yet loaded.

`EditorGenerateScreen` owns its request and staging editor. It presents the
progress screen before generating, samples the same candidate budget as native
clients and transfers the editor only on success. The screen stack admits queued
input before advancing work, so a queued cancellation cannot lose to a completed
roll. Failure and cancellation destroy the staging editor and leave the live editor unchanged.

## Latency limits

A generator roll is synchronous. Browser preview polling yields between complete
attempts, not inside every terrain algorithm. Large or expensive maps can still
pause rendering and input until a roll finishes. Cancellation can discard queued
work but cannot interrupt a roll in progress. Subdividing the new generator
pipeline is follow-up work; the old port's coroutine checkpoints do not apply to
the shared generators.

## Compatibility and verification

Browser generation uses the shared service. Existing map/save formats and the save-loader
compatibility floor remain intact. The generator's native golden-map tests remain
in CI. Native session tests cover cancellation, RNG restoration, invalid requests
and staged fertility publication against the shared `Fertility::Field`.

Native/WebAssembly simulation checks use the same retained saved-game bytes and
compare per-tick checksums. This does not prove that every generator produces
bit-identical maps across platforms: floating-point terrain generation needs
separate qualification. Online rooms and LAN hosts distribute the selected map file (checked by its hash)
rather than asking clients to independently regenerate it, so all clients start
with the same map bytes.

Related: [browser guide and decision index](README.md).
