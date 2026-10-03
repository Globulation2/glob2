from pathlib import Path
from PIL import Image
root=Path('artifacts/skins/merge-validation/fog-gate')
bg=(45,50,60)
images={}
for kind in ('mesh','shadow'):
 for alpha in (255,128,0):
  name=f'opacity-{kind}-{alpha}'
  im=Image.open(root/(name+'.bmp')).convert('RGB')
  im.save(root/(name+'.png'))
  images[kind,alpha]=im
  if alpha==0: assert all(pixel==bg for pixel in im.getdata()),f'{kind} at zero opacity changed pixels'
opaque=list(images['mesh',255].getdata()); half=list(images['mesh',128].getdata())
changed=sum(pixel!=bg for pixel in opaque)
assert changed>1000, f'Missing rendered mesh: {changed} pixels'
error=max(abs(q[i]-(bg[i]+(p[i]-bg[i])*128/255)) for p,q in zip(opaque,half) for i in range(3))
assert error<=2, f'Premultiplied opacity blend error: {error}'
shadow=images['shadow',128]; clear=images['mesh',255]
expected=tuple(bg[i]+((90,70,50)[i]-bg[i])*128/255 for i in range(3))
count=0; shadow_error=0
for y in range(82,334):
 for x in range(82,334):
  if clear.getpixel((x,y))==bg:
   count+=1
   shadow_error=max(shadow_error, max(abs(shadow.getpixel((x,y))[i]-expected[i]) for i in range(3)))
assert count>1000 and shadow_error<=2,(count,shadow_error)
print(f'PASS {changed} mesh pixels; half-opacity maximum channel error {error:.3f}/255')
print(f'PASS {count} shadow-only pixels; maximum channel error {shadow_error:.3f}/255')
print('PASS both zero-opacity captures equal the background exactly')
