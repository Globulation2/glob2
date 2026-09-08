# Globulation 2 high-resolution pack v1

Default 4× artwork for OpenGL sessions. Disable High-resolution artwork in General Settings to select classic artwork on the next session load. Original game assets are required for logical geometry and fallback.

`frames.txt` is the runtime index (`GLOB2_HIGHRES 1`). Each row contains frame ID, original logical width/height, scale, base PNG and team PNG (`-` means absent). `manifest.json` records provenance, SHA-256 hashes, per-layer dimensions, and terrain atlas layout. Green team layers follow the engine's hue-shift convention.

Ship this directory intact. Invalid or incomplete frames fall back to originals. Terrain mip PNGs must correspond to the frame PNGs and retain their extruded borders. Runtime does not require Python or AI tooling. Reproduction and validation instructions are in `docs/high-resolution/README.md` in the source checkout.
