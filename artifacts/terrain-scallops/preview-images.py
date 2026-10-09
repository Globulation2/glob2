from PIL import Image, ImageDraw
from pathlib import Path
p = Path('artifacts/terrain-scallops')
grass_tile = Image.open('data/gfx/terrain0.png').convert('RGB')
sand_tile = Image.open('data/gfx/terrain128.png').convert('RGB')
grass = Image.new('RGB', (768, 512))
sand = grass.copy()
for y in range(0, 512, 32):
    for x in range(0, 768, 32):
        grass.paste(grass_tile, (x, y))
        sand.paste(sand_tile, (x, y))
canvas = Image.new('RGB', (1536, 542), (25, 28, 30))
draw = ImageDraw.Draw(canvas)
for i, name in enumerate(['previous-pass', 'after']):
    mask = Image.open(p / (name + '.pgm'))
    result = Image.composite(grass, sand, mask)
    result.save(p / (name + '.png'))
    canvas.paste(result, (i * 768, 30))
    draw.text((i * 768 + 12, 9), 'Previous pass' if i == 0 else 'Deeper lobes and scallops', fill='white')
canvas.save(p / 'deeper-comparison.png')
