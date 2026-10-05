## Final base refresh

The final pre-merge fetch advanced master to `0e6f17204` via editor preview fix `eacc18128` (#807) and artwork package #220. The only new C++ edits select unit animation frames in `MapEditDraw.cpp` and `WidgetsTools.cpp`; no simulation, map mutation, gradient, dependency/toolchain or CI source changed. The artwork change updates presentation data and its packaging/validation tools.

Both updated editor translation units passed syntax compilation against the tested PR's Map interfaces with the same native compiler/flags. `latest-editor-commands.json`, `latest-editor.log` and the exact source snapshots record this additional integration check. Source `b2850bb8b` remains unchanged and mergeable. Full UI/assets validation was not repeated for the independent artwork package; simulation/performance evidence remains on the stated tested source and assets.
