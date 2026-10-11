from pathlib import Path
from collections import Counter
import json
O=Path(__file__).resolve().parent

def category(stack):
 if 'GAGCore::AssetImage::prepareUpload' in stack:return 'Prepared upload pixels and mip chains'
 if 'decodeImage (AssetLoader.cpp)' in stack:
  if 'SDL_CreateSurface' in stack:return 'Decoded pixels, including adopted CPU drawables'
  if 'WebPDecode' in stack:return 'WebP decoder scratch'
  return 'Image decoder metadata and other scratch'
 if 'SpriteLoad.cpp' in stack and 'SDL_CreateSurface' in stack:return 'Native sprite preparation and copies'
 if 'DrawableSurface::initTextureSize' in stack:
  if '0x' in stack:return 'Texture initialization / opaque driver allocations'
  return 'Texture initialization CPU buffers'
 if '0x' in stack:return 'Opaque driver / library allocations'
 if 'AssetLoader.cpp' in stack:return 'Asset loader metadata, compressed inputs and queues'
 if 'SDL_CreateSurface' in stack:return 'Other SDL surfaces'
 if 'Font' in stack or 'TTF_' in stack:return 'Fonts and text'
 return 'Other heap allocations'

report={}
for name in ['baseline','a','b','ab']:
 report[name]={}
 for phase in ['startup','reload']:
  d=O/f'latest-heap-{name}-{phase}';counts=Counter()
  for line in (d/'peak.stacks').read_text().splitlines():
   stack,bytes_=line.rsplit(' ',1);counts[category(stack)]+=int(bytes_)
  total=sum(counts.values())
  report[name][phase]={'peak_bytes':total,'categories':[{'category':k,'bytes':v,'percent':100*v/total} for k,v in counts.most_common()]}
(O/'heap-breakdown.json').write_text(json.dumps({'scope':'Exclusive allocation-origin attribution at heaptrack global heap peak; each allocation counted once. Ownership transfers preserve the original decode allocation stack. Opaque driver frames cannot be attributed more precisely without symbols. Instrumented runs are separate from performance trials. This is loading/reload evidence, not a whole-match peak profile.','variants':report},indent=2))
for row in report['ab']['reload']['categories']:print(row['category'],round(row['bytes']/1048576,2),'MiB')
