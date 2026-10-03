# SPDX-License-Identifier: GPL-3.0-or-later
"""The established 2x2 generation prompt, independent of provider and raster IO.

Keep this text stable during structural refactors: prompt changes are authoring
behavior changes and should be evaluated with independently generated images.
The selected examples are context; the user's concept remains authoritative.
"""

from __future__ import annotations

import json


def image_prompt(concept, selection, entries):
    roles = []
    for index, example in enumerate(selection["examples"], 1):
        description = entries[example["generator_id"]]["description"]
        roles.append(
            f"Reference {index}: {example['generator_id']}. Description: {description}\n"
            f"Why relevant: {example['reason']}\n"
            f"Borrow: {json.dumps(example['borrow'])}. Avoid: {json.dumps(example['avoid'])}."
        )
    return (
        """Use case: infographic-diagram. Asset type: categorical Globulation 2 map data.
Design ONE complete square four-player map, then output FOUR identical copies in a 2x2 square mosaic touching edge to edge. Each quadrant is the entire map with four colony homes, sixteen markers total. Do not rotate, mirror or vary copies. Use approximately256x256 underlying cells per tile,512x512 cells over the full mosaic, enlarged with hard pixel edges.

Concept (applies to EACH complete repeated tile):
"""
        + concept
        + "\n\n"
        + "\n\n".join(roles)
        + """

Input images are SQUARE REFERENCE SHEETS, not edit targets. Each sheet contains one or two labeled example mosaics side by side within its middle band; EACH example itself repeats a complete four-player map exactly2x2. Sheet1 contains references1 and2, sheet2 references3 and4, and so on. All examples listed above are available in these sheets. The labels, unused dark space and sheet layout identify examples only: NEVER copy these labels, panels or margins into the output. Learn their relevant geography and style as described, categorical representation and continuous repetition. Make NEW geography rather than copying a reference. The concept takes precedence over example details; do not add unrelated ridges, enclosed towns, formal geometry or resource barriers merely because an example contains them.

Use flat opaque colors ONLY: grass#008000, sand#F0DC8C, water#0040FF, wood#004000, wheat#FFFF00, stone#808080, algae#00FFFF, papyrus#FF00FF, cherry#FF0000, orange#FF8000, prune#8000FF, colony#FFFFFF. White ONLY for exactly four small colony squares per complete tile, sixteen total. No sprites, buildings, shading, textures, gradients, antialiasing, labels, grid lines, borders, gutters, panels or perspective.

Every colony has spacious open green building ground, substantial yellow wheat and compact dark-green wood within about8 map cells via unobstructed paths. Wheat adjoins blue water over at most a thin tan shore. Keep wood off food plots, with dry ground or tan margins. All four homes must have visible exits and connected walking routes; deliberate crossings or barrier gaps are at least8 cells wide. Never enclose a home in water, wood or stone without an open walking exit. Preserve the intended distinct home regions and strategic routes from the concept.

The underlying map is a wrapping torus. Features crossing an edge continue at the same position, width, direction and color in the neighboring copy: terrain AND wood, wheat and stone. Internal joins are ordinary uninterrupted landscape, never borders. Outer mosaic edges also repeat. Each location(x,y) agrees with(x+halfwidth,y),(x,y+halfheight),(x+halfwidth,y+halfheight). Output the FULL2x2 mosaic filling the square canvas edge to edge, not one map split into four player regions.

Final output contract: a SQUARE image, width equal to height, with ONE newly designed square map repeated four times. Exactly16 white markers total, exactly4 in each quarter. One white marker per home, no duplicate paired markers. The reference sheet framing and multiple DIFFERENT reference maps must not appear in the output. Fill the entire square with continuous map geography; no dark margins, no comparison layout, no rectangular image.
Categorical data is essential: every wood grove is a uniformly filled dark-green polygon, NOT little tree pictures; every stone outcrop is a uniformly filled mid-gray#808080 polygon, NOT rocks with pale highlights. Wheat is a uniformly filled yellow polygon, NOT grain sprites. Never use pale-gray, cream or white highlights, because they would be decoded as extra colonies. The only white anywhere is the16colony squares. All regions have solid interiors in the stated exact palette.
"""
    )
