import sys, glob, os
from PIL import Image, ImageChops
a, a2, b = sys.argv[1:4]
for f in sorted(glob.glob(a + '/*.bmp')):
    n = os.path.basename(f)
    A = Image.open(f).convert('RGB'); A2 = Image.open(os.path.join(a2, n)).convert('RGB'); B = Image.open(os.path.join(b, n)).convert('RGB')
    noise = ImageChops.difference(A, A2).getbbox()
    d = ImageChops.difference(A, B)
    box = d.getbbox()
    if box is None:
        print(f'{n}: identical'); continue
    # Count differing pixels outside the master-vs-master noise box.
    px = d.load(); outside = 0; total = 0
    for y in range(box[1], box[3]):
        for x in range(box[0], box[2]):
            if px[x, y] != (0, 0, 0):
                total += 1
                if not (noise and noise[0] <= x < noise[2] and noise[1] <= y < noise[3]):
                    outside += 1
    print(f'{n}: {total} px differ in {box}; master run-to-run noise {noise}; outside noise: {outside}')
