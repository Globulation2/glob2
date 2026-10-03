# Colony skins validation evidence

This evidence branch is separate from the game source. Renderer profiling was
recorded at be4a0d621; corrected model assets at 174afa553. These commits are
preserved on codex/colony-skins-before-master-integration locally. The PR
replays the feature onto current master and records new integration checks
separately. Earlier measurements are not performance claims for a new base.

Three alternating matched process pairs: 180 frames each, 32 warmup, 488 added
units, two paints, 800x600. See performance/compare.py for invocation and
results.json for all timing data. Each draw checks simulation state and classic
and skinned per-frame checksums match. Driver: llvmpipe software OpenGL; no
hardware GPU timing claim. Median per-run means: 157.492 to 23.870 ms (84.8%).
Cold rendering did not improve; peak process RSS rose by about 45.1 MiB.
Classic captures were exact; 69 of 480000 skinned pixels differed by at most
6/255 due to atlas placement. Corrected-assets pair retained about 84%.

The screenshots show the corrected original-derived meshes and a real
two-client match with authorized paints. Swarm geometry is reconstructed
TRELLIS artwork; other buildings remain sprites. Native mobile retains classic
fallback. Real Stripe test-account checkout and hardware GPU/platform/feel
validation remain release limits. Purchases are unavailable without Stripe
server configuration, and live keys require explicit server opt-in.
