#!/usr/bin/env python3
"""Document visual source identities; these comparison previews are not runtime exports."""
import json
import math
from pathlib import Path
from urllib.parse import quote
from PIL import Image, ImageDraw
from import_originals import ROOT, ART

rows = json.loads((ART/'provenance/building-map.json').read_text())
previews = ROOT/'.cache/original-art/previews'
output = ROOT/'docs/original-artwork'
output.mkdir(parents=True, exist_ok=True)
pair_width, row_height, columns = 560, 260, 2
sheet = Image.new('RGB', (pair_width*columns, row_height*math.ceil(len(rows)/columns)), '#28343d')
draw = ImageDraw.Draw(sheet)
lines = ['# Visually verified building identities', '',
    'All 16 recovered building XCFs have been compared with current game sprites, including hidden base/team layers. '
    'The family and level match is confirmed. Completed school and first/second racetrack exports are now verified; other state/anchor/layer combinations still require validation. See RECOVERED-RUNTIME.md for active recipes. '
    'Levels in folder names are human-facing (1–3); runtime suffixes use 0–2.', '',
    '![Original source beside current game sprite](../../docs/original-artwork/building-matches.jpg)', '',
    'Each pair shows the recovered source preview on the left and the current classic game sprite on the right. '
    'Images are fitted to equal comparison boxes to compare geometry, not to imply equal native resolution. '
    'Selected hidden layers are enabled only in disposable previews; no XCF is resaved.', '',
    '| Historical source | Game reference | Identification evidence |', '| --- | --- | --- |']
for i, row in enumerate(rows):
    name = Path(row['historical_name']).stem
    source = Image.open(previews/(name+'.png')).convert('RGBA')
    runtime = Image.open(ROOT/'data/gfx'/(row['reference_frame']+'.png')).convert('RGBA')
    team_path = ROOT/'data/gfx'/(row['reference_frame']+'r.png')
    if team_path.exists():
        team = Image.open(team_path).convert('RGBA')
        combined = Image.new('RGBA', (max(runtime.width,team.width),max(runtime.height,team.height)))
        combined.alpha_composite(runtime); combined.alpha_composite(team); runtime = combined
    x=(i%columns)*pair_width; y=(i//columns)*row_height
    for column, (label, image) in enumerate([(name,source),(row['reference_frame'],runtime)]):
        factor = min(250/image.width,220/image.height)
        image = image.resize((round(image.width*factor),round(image.height*factor)),Image.Resampling.LANCZOS if column==0 else Image.Resampling.NEAREST)
        left=x+column*280
        draw.text((left+8,y+6),label,fill='white')
        sheet.paste(image,(left+(280-image.width)//2,y+28+(220-image.height)//2),image)
    link=quote(row['source'].removeprefix('datasrc/gfx/'))
    lines.append(f"| [{row['historical_name']}]({link}) | `{row['reference_frame']}` | {row['evidence']} |")
lines += ['', 'The two third-level barracks sources are retained as distinct variants. A hidden team layer is not a missing building. '
    'For example, `building10` initially shows only green markings; its hidden base establishes the second racetrack identity.', '',
    'Recreate with GIMP 2.10 Python support from the repository root, followed by Pillow:', '', '```sh',
    'gimp-console -n -i -d -f -c --batch-interpreter=python-fu-eval \\',
    '  -b \'execfile("tools/artwork/preview_buildings_gimp.py")\' -b \'pdb.gimp_quit(0)\'',
    'python3 tools/artwork/building_comparisons.py', '```', '']
sheet.save(output/'building-matches.jpg',quality=92)
(ART/'BUILDING-MAP.md').write_text('\n'.join(lines))
