# Saved-game continuation

Save-format version 91 preserves the live Mersenne Twister state and routing state in saved games. Keeping only the original seed restarts the random sequence on load. Rebuilding cached gradients also changes unit decisions: these fields intentionally lag map edits until their scheduled refresh.

New saved games retain:

- The canonical 624-word Mersenne Twister state, written as fixed-width integers.
- Immobile-unit occupancy, clearing reservations, both fog buffers and the active buffer.
- Allocated resource, forbidden, guard and clearing gradients, including refresh scheduling flags.
- Building gradients, dirty flags, refresh ticks and cached access results.

Map files do not contain this runtime section. Older saves remain readable and use the previous seed-based initialization; their missing historical state cannot be recovered. Version 91 saves require a version 91-capable reader. The live RNG is installed only after successful loading, and replacing the saved player header with the same seed preserves it.

`savegame-safety-test` compares 300 subsequent simulation steps against uninterrupted play, for human and AI players, after advancing the random sequence and loading a mid-game checkpoint. It also checks 2,000 future RNG outputs, text round trips of the routing section, and rejection/reuse after truncated or invalid runtime sections. The normal save loader and production simulation execute these checks.

The earlier Windows OpenGL “background bleed-through” observation was the existing cloud overlay. With the same build and saved game, setting only `cloudMaxAlpha=0` removed the moving shapes. No production renderer change was required. `map-render-resize-test` additionally checks opaque rectangle and alpha-map drawing after a menu background and cached-frame presentation.
