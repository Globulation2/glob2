# Native browser reference refresh

See revision-mapping.json for precise tested/final source identities and evidence.json for native binary, input, preview and before/after fixture hashes. Required native traces are studio-native.checksums.gz (1024ticks) and generator-native.checksums.gz (64ticks); decompress before checking the pinned raw SHA256. Both reproduce the existing pins byte-for-byte on SIM121/save193. Generator initial-save reload matched the entire generated trace; the duplicate trace is omitted.

commands.sh records exact native/harness/Node commands using isolated /tmp profiles. Node174/174 and nativegeneratorStudio1/1 passed. Independentreview found no actionablefinding. No rebuilt browserruntime matrix was run locally. No raw profiles, savedgames or redundant supplemental probes are exported. The optional Studio save-load comparison in evidence.json retains its honest aggregate-only old-map-format difference; it was not substituted for the pinned fresh-map trace.
