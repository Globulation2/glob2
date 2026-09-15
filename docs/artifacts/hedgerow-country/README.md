# Hedgerow Country review evidence

These are native map-preview captures, not in-game camera screenshots. Dark green marks
wooded boundaries, yellow marks sand lanes and farm containment, blue marks ponds, and
numbered colours mark colony starts.

The complete review bundle (request JSON and gzip-compressed saved maps for each preview,
the bulk plans and per-request rows, the 80 playtest result records, two sample replays,
platform test logs and the translation audit) lives on the
[evidence/hedgerow-country branch](https://github.com/Globulation2/glob2/blob/evidence/hedgerow-country/docs/artifacts/hedgerow-country/README.md), which keeps
those archives out of the main source history.

## Default layout

256×256, four colonies, seed 19, default controls.

![Default Hedgerow Country layout](default.png)

## Large stress case

512×512, eleven colonies, seed 4132429259. This fresh-seed case exercised the shared
boundary-clearance repair; the hedge thickness and designed openings remain.

![Large Hedgerow Country stress case](large.png)

## Known balance limit

512×512, eleven colonies, seed 510294: the inspected baseline outlier, with a
weakest/strongest exclusive walking-territory ratio of 0.135. Connected roads and
starting supplies do not imply equal expansion opportunities.

![Uneven expansion territory example](balance-outlier.png)
