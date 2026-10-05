from pathlib import Path
import hashlib
root=Path(__file__).resolve().parent
case=Path('TrailReview/Trail_review_editor_and_moving_colony_display_artifacts')
for mode in ['software','opengl']:
 a=(root/'before'/case/f'checksums-{mode}.txt').read_bytes()
 b=(root/'after'/case/f'checksums-{mode}.txt').read_bytes()
 assert a==b, f'{mode}: first difference '+str(next((i for i,(x,y) in enumerate(zip(a.splitlines(),b.splitlines())) if x!=y), 'length'))
 print(mode, len(a.splitlines()), 'matching per-tick checksums',hashlib.sha256(a).hexdigest())
a=(root/'after'/case/'checksums-software.txt').read_bytes();b=(root/'after'/case/'checksums-opengl.txt').read_bytes();assert a==b
print('Software/OpenGL simulation traces also match')
