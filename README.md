The baseline is the locally built release benchmark from the rollback playtest state before the GPU animation pass. It has no separate committed revision. The recorded binary digest identifies it. The final run uses the committed renderer sources, with two-tile shore calming. The baseline has one-tile calming.

Run run.sh from a source checkout with GLOB2_BASELINE_BINARY and GLOB2_CANDIDATE_BINARY pointing to the release benchmark executables. Arguments are variant name, zoom, and optional pan. Variant names starting with baseline select the baseline binary. Each run uses 120 warmup frames followed by 180 measured frames, the included seed-713 Honeycomb save, HD assets, and Legacy OpenGL. The requested 1280×800 window resolved to a 1120×800 map viewport.

Both binaries reused existing packaged runtime artwork. Results include draw completion and readback overhead and exclude final presentation. The recorded macOS environment and all raw measured frames accompany the summary. The first pan run contains a 260 ms wall-time outlier. A repeat of both baseline and final routes is retained separately.

The final code passed six isolated display tests. The HD water case compares consecutive GPU readbacks, checks mixed shoreline/static-page reuse, and verifies that changed discovery hides both passes. The calm case checks the two-tile falloff.

Shader validation covered the generated desktop GL, WebGL 2, and Vulkan direct-terrain fragment shaders and the GLSL 1.20 animation variant with glslangValidator. Other platforms were not runtime-tested.
