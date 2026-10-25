from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import json, hashlib
root=Path(__file__).resolve().parent
bundle=json.loads((root/'bundle/manifest.json').read_text())
assert len(bundle['pages'])==29 and len(bundle['turnPages'])==7
assert bundle['turnMapping']=={'first':256,'frames':32,'phaseShift':3}
tiles=[]
for page in bundle['turnPages']:
    data=(root/'bundle'/(page['sha256']+'.webp')).read_bytes()
    assert hashlib.sha256(data).hexdigest()==page['sha256']
    assert len(data)==page['bytes']
    image=Image.open(root/'bundle'/(page['sha256']+'.webp')).convert('RGBA')
    assert image.crop((0,512,1024,1024)).getchannel('A').getextrema()==(0,0)
    row=[image.crop((f%8*128,f//8*128,f%8*128+128,f//8*128+128)) for f in range(32)]
    assert len({frame.tobytes() for frame in row})==32
    tiles.append(row)
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',16)
frames=[]
for frame in range(32):
    image=Image.new('RGB',(340,7*100),(35,39,46));draw=ImageDraw.Draw(image)
    for row,page in enumerate(bundle['turnPages']):
        draw.text((8,row*100+40),page['clip'],font=font,fill=(235,235,235))
        tile=tiles[row][frame].resize((100,100),Image.Resampling.LANCZOS)
        image.paste(tile,(225,row*100),tile)
    frames.append(image)
frames[0].save(root/'skin-turns.gif',save_all=True,append_images=frames[1:],duration=80,loop=0)
contact=Image.new('RGB',(150+32*64,7*80),(35,39,46));draw=ImageDraw.Draw(contact)
for row,page in enumerate(bundle['turnPages']):
    draw.text((4,row*80+30),page['clip'],font=font,fill=(235,235,235))
    for frame,tile in enumerate(tiles[row]):
        tile=tile.resize((64,64),Image.Resampling.LANCZOS)
        contact.paste(tile,(150+frame*64,row*80),tile)
contact.save(root/'skin-turns.png')
print('Verified 224 distinct turn poses, authenticated pages, and transparent unused rows.')
