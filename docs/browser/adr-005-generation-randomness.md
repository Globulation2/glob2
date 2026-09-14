# ADR 005: generation ownership and scheduling

Status: accepted.

The browser uses master's `GenerationService`, registered generators and
`GenerationRequest` options. It preserves the current lobby layout, landscape
picker, start-quality scoring, candidate selection and generated-map snapshot.
The port does not retain the replaced Perlin/legacy generator implementation.

`GenerationContext` owns named random streams. `GenerationService` saves and
restores the calling thread's synchronized engine RNG around each complete roll.
Native landscape previews retain their worker threads. The single-threaded browser
calls `LandscapePreviewer::poll()` once per UI timer to complete one queued preview;
no native thread is started in WebAssembly. The lobby and landscape picker both
service this queue. Only a completed candidate can become a launch snapshot.

Landscape and start-quality dialogs are owned children of `ScreenStack`. The
selected request and seed are copied back before the child is destroyed. A
launched custom game's snapshot owner is transferred to `GameLoadScreen`, so
closing the lobby cannot delete a map that has not yet loaded.

`EditorGenerateScreen` owns its request and staging editor. It presents the
progress screen before generating, samples the same candidate budget as native
master and transfers the editor only on success. The screen stack admits queued
input before advancing work, so a queued cancellation cannot lose to a completed
roll. Failure and cancellation destroy the staging editor and restore RNG state.

## Latency limits

A generator roll is synchronous. Browser preview polling yields between complete
candidates, not inside every terrain algorithm. Large or expensive maps can still
pause rendering and input until a roll finishes. Cancellation can discard queued
work but cannot interrupt a roll in progress. Subdividing the new generator
pipeline is follow-up work; the old port's coroutine checkpoints do not apply to
master's replacement generators.

## Compatibility and verification

Generation behavior follows master. Existing map/save formats and the save-loader
compatibility floor remain intact. The generator's native golden-map tests remain
in CI. Native session tests cover cancellation, RNG restoration, invalid requests
and staged fertility publication against master's `Fertility::Field`.

Native/WebAssembly simulation checks use the same retained saved-game bytes and
compare per-tick checksums. This does not prove that every generator produces
bit-identical maps across platforms: floating-point terrain generation needs
separate qualification. YOG distributes the host's selected map file rather than
asking clients to independently regenerate it, so all clients start with the same
map bytes.
