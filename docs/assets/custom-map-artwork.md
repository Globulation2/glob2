# Custom map artwork

Companion to [terrain materials](terrain-materials.md).

## Map-owned custom artwork

The online set workspace and editor import use the same terrain material parser
and compositor, with sprite paths resolved from the immutable map bundle rather
than the global Toolkit cache. Custom materials bind by stable terrain key, can
use the installed boundary profiles and have independent decor sheets. Built-in
artwork stays installed; it is never copied into a map bundle. See
[themed sets](../features/terrain-resource-sets.md#themed-terrain-and-resource-sets) for
authoring, frame bounds, attribution and offline sharing.

Sheets are identified by their PNG content hash. When combining sets, identical
PNG bytes must use the same frame width and height; conflicting frame grids are
rejected without changing the map. Use a distinct sheet image when the same art
needs a different grid. Missing bundled sheets are validation errors. Older
manually imported definitions that reference installed artwork retain their
ordinary missing-art fallback.

Related: [asset production](README.md).
