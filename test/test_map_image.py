#!/usr/bin/env python3
"""Exercise PNG map import/export without display, network access, or extra Python packages."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import zlib

from test_map_cli import png
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from build_paths import native_binary

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1] if len(sys.argv) > 1 else native_binary()).resolve()
OUT = ROOT / 'artifacts/map-image'
COLORS = [(0,128,0),(240,220,140),(0,64,255),(0,64,0),(255,255,0),(128,128,128),
          (0,255,255),(255,0,255),(255,0,0),(255,128,0),(128,0,255),(255,255,255),
          (190,225,240),(176,138,98)]


def write_png(path, w, h, cells, transparent=False):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data)&0xffffffff)
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        for x in range(w):
            rows.extend(COLORS[cells[y*w+x]])
            if transparent: rows.append(0 if x == y == 0 else 255)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w,h,8,6 if transparent else 2,0,0,0))
    path.write_bytes(data + chunk(b'IDAT',zlib.compress(rows)) + chunk(b'IEND',b''))


def mark(cells,w,h,x,y):
    for dy in range(-1,6):
        for dx in range(-1,6): cells[((y+dy)%h)*w+(x+dx)%w]=11


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, GLOB2_USER_DATA_DIR=str(OUT/'profile'), SDL_VIDEODRIVER='invalid')
    logs=[]
    def run(*args, ok=True, default_seams=False):
        if not default_seams and len(args) >= 2 and args[:2]==('map', 'import-image') and '--image-seam-width' not in args:
            args=(*args,'--image-seam-width',0)
        command=[str(BINARY), *map(str,args), '--data-dir', str(ROOT)]
        try:
            result=subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)
        except subprocess.TimeoutExpired as error:
            logs.append(dict(args=command,exit=None,timeout_seconds=error.timeout,
                             stdout=(error.stdout or b'').decode('utf-8', errors='replace'),
                             stderr=(error.stderr or b'').decode('utf-8', errors='replace')))
            (OUT/'commands.json').write_text(json.dumps(logs,indent=2)+'\n')
            raise
        logs.append(dict(args=command,exit=result.returncode,stdout=result.stdout,stderr=result.stderr))
        (OUT/'commands.json').write_text(json.dumps(logs,indent=2))
        assert result.returncode == 0 if ok else result.returncode in (2, 3), logs[-1]
        return result
    w=h=64
    cells=[0]*(w*h)
    # Every resource has an interior patch; algae implies water, all others grass.
    for c,(x,y) in enumerate([(12,12),(24,12),(36,12),(48,12),(12,28),(24,28),(36,28),(48,28)],3):
        for dy in range(6):
            for dx in range(6): cells[(y+dy)*w+x+dx]=c
    mark(cells,w,h,62,62)
    cells[45*w+45]=11  # ignored marker speck
    image=OUT/'palette.png'; write_png(image,w,h,cells)
    dest=OUT/'palette.map.gz'; report=OUT/'palette.json'
    args=['map', 'import-image',image,'--width',64,'--height',64,'--teams',1,'--seed',19]
    run(*args,'--output',dest,'--report-file',report)
    data=json.loads(report.read_text())
    assert data['image_import']['markers']==1 and data['image_import']['ignored_markers']==1
    assert data['image_import']['dropped_resources']>0
    assert data['map']['colonies'][0]['units']['workers'] == 4
    for resource in ('wood','wheat'):
        amounts=data['resources']['types'][resource]['amount_per_deposit']
        assert 1 <= amounts['min'] <= amounts['max'] <= 4
        assert amounts['max'] > 1 and amounts['mean'] > 1
    exported=OUT/'export.png'; run('map', 'export-image',dest,'--output',exported)
    ew,eh,pixels=png(exported); assert (ew,eh)==(64,64)
    for color in COLORS[3:11]: assert bytes(color) in [pixels[i:i+3] for i in range(0,len(pixels),3)],color
    # Catalogue vertices survive authoring, save/load, and image round trips.
    # Beaches reshape only grass/water contact: ice and road keep their vertices.
    materials=[0]*(64*64)
    for y in range(20,36):
        for x in range(12,20): materials[y*64+x]=12  # ice
        for x in range(20,28): materials[y*64+x]=13  # road
        for x in range(28,36): materials[y*64+x]=2   # water
    mark(materials,64,64,48,48)
    material_image=OUT/'new-terrain.png';write_png(material_image,64,64,materials)
    material_map=OUT/'new-terrain.map.gz';material_report=OUT/'new-terrain.json'
    run('map', 'import-image',material_image,'--width',64,'--height',64,'--teams',1,
        '--output',material_map,'--report-file',material_report)
    material_data=json.loads(material_report.read_text())
    for name in ('ice','road'):
        assert material_data['terrain'][name]['tiles']==128
    material_export=OUT/'new-terrain-export.png'
    run('map', 'export-image',material_map,'--output',material_export)
    _,_,material_pixels=png(material_export)
    for x,y,kind in ((11,24,0),(12,24,12),(20,24,13),(27,24,13),(28,24,2)):
        i=(y*64+x)*3
        assert material_pixels[i:i+3]==bytes(COLORS[kind]),(x,y,kind)
    run('map', 'preview',material_map,'--report-file',OUT/'new-terrain-loaded.json')
    assert json.loads((OUT/'new-terrain-loaded.json').read_text())['terrain']==material_data['terrain']
    run('map', 'import-image',material_export,'--width',64,'--height',64,'--teams',1,
        '--output',OUT/'new-terrain-restored.map.gz','--report-file',OUT/'new-terrain-restored.json')
    restored_materials=json.loads((OUT/'new-terrain-restored.json').read_text())['terrain']
    for name in ('ice','road'):
        assert restored_materials[name]==material_data['terrain'][name]
    # Canonical image export/import must preserve the full live team capacity.
    dense=[0]*(256*256)
    for y in range(32,256,64):
        for x in range(32,256,64): mark(dense,256,256,x,y)
    dense_image=OUT/'sixteen.png';write_png(dense_image,256,256,dense)
    dense_map=OUT/'sixteen.map.gz';dense_report=OUT/'sixteen.json'
    run('map', 'import-image',dense_image,'--width',256,'--height',256,'--teams',16,
        '--seed',19,'--output',dense_map,'--report-file',dense_report)
    first=json.loads(dense_report.read_text())
    assert first['image_import']['markers']==16 and len(first['map']['colonies'])==16
    dense_export=OUT/'sixteen-export.png'
    run('map', 'export-image',dense_map,'--output',dense_export)
    run('map', 'import-image',dense_export,'--width',256,'--height',256,'--teams',16,
        '--seed',19,'--output',OUT/'sixteen-restored.map.gz','--report-file',OUT/'sixteen-restored.json')
    restored=json.loads((OUT/'sixteen-restored.json').read_text())
    assert restored['image_import']['markers']==16 and len(restored['map']['colonies'])==16
    assert [c['start'] for c in restored['map']['colonies']]==[c['start'] for c in first['map']['colonies']]
    mark(dense,256,256,0,0)
    excessive=OUT/'seventeen.png';write_png(excessive,256,256,dense)
    rejected=run('map', 'import-image',excessive,'--width',256,'--height',256,
        '--output',OUT/'seventeen.map.gz',ok=False)
    assert '1..16 colony markers (observed 17)' in rejected.stdout+rejected.stderr
    assert not (OUT/'seventeen.map.gz').exists()
    # Offset river banks must meet across the repaired wrap seam, including corners.
    river=[0]*4096
    for y in range(64):
        for x in range(64):
            center=20 if x<32 else 26
            if abs(y-center)<=7:river[y*64+x]=2
    mark(river,64,64,32,50)
    river_image=OUT/'offset-river.png';write_png(river_image,64,64,river)
    def terrain_codes(path):
        _,_,px=png(path)
        return [0 if tuple(px[i:i+3])==COLORS[0] else
                1 if tuple(px[i:i+3])==COLORS[1] else
                2 if tuple(px[i:i+3])==COLORS[2] else 0 for i in range(0,len(px),3)]
    for width in (0,8):
        path=OUT/f'river-{width}.map.gz';rp=OUT/f'river-{width}.json';ep=OUT/f'river-{width}.png'
        run('map', 'import-image',river_image,'--width',64,'--height',64,'--teams',1,
            '--image-seam-width',width,'--output',path,'--report-file',rp)
        run('map', 'export-image',path,'--output',ep)
        grid=terrain_codes(ep)
        mismatch=sum(grid[y*64]!=grid[y*64+63] for y in range(64))
        info=json.loads(rp.read_text())['image_import']
        assert info['seam_width']==width
        if width:
            assert mismatch==0 and info['seam_terrain_changes']>0
            assert all(grid[x]==grid[63*64+x] for x in range(64))
            assert any(grid[y*64]==2 for y in range(64)), 'River crossing was erased'
        else:assert info['seam_terrain_changes']==0
    run('map', 'import-image',river_image,'--width',64,'--height',64,'--teams',1,
        '--output',OUT/'default-seam.map','--report-file',OUT/'default-seam.json',default_seams=True)
    assert json.loads((OUT/'default-seam.json').read_text())['image_import']['seam_width']==2
    # Default repair must be a no-op for already aligned, legally placed groves.
    # The groves sit outside the colony protection square and inside the seam band,
    # so this tests alignment preservation rather than protection masking.
    aligned=[0]*4096
    for y in range(3,10):
        for x in (0,1,2,61,62,63):aligned[y*64+x]=3
    # A narrow wrapped lake with a legal sand rim is also already aligned.
    # Its water lies inside the repair band but outside its interpolation anchors.
    for y in range(20,26):
        for x in (0,1,62,63):
            aligned[y*64+x]=2 if 21<=y<25 and x in (0,63) else 1
    mark(aligned,64,64,32,32)
    for defect in (False,True):
        source=aligned.copy()
        if defect:
            # A distant terrain/resource mismatch must not reshape the lake/grove.
            source[53*64]=1
            for y in range(56,59):
                for x in (0,1,2):source[y*64+x]=4
            for y in range(57,60):
                for x in (61,62,63):source[y*64+x]=4
        ip=OUT/f'aligned-{defect}.png';write_png(ip,64,64,source)
        exports=[]
        for enabled in (False,True):
            mp=OUT/f'aligned-{defect}-{enabled}.map.gz'
            rp=OUT/f'aligned-{defect}-{enabled}.json'
            ep=OUT/f'aligned-{defect}-{enabled}-export.png'
            options=[] if enabled else ['--image-seam-width',0]
            run('map', 'import-image',ip,'--width',64,'--height',64,'--teams',1,
                '--seed',19,*options,'--output',mp,'--report-file',rp,default_seams=True)
            run('map', 'export-image',mp,'--output',ep)
            exports.append(png(ep)[2])
            if enabled and not defect:
                info=json.loads(rp.read_text())['image_import']
                assert info['seam_width']==2
                assert info['seam_terrain_changes']==info['seam_shore_changes']==info['seam_resource_changes']==0
        if not defect:assert exports[0]==exports[1], 'Aligned map changed under default repair'
        for y in range(3,10):
            for x in (0,1,2,61,62,63):
                i=(y*64+x)*3
                assert exports[0][i:i+3]==exports[1][i:i+3]==bytes(COLORS[3]), 'Distant defect damaged aligned grove'
        for y in range(21,25):
            for x in (0,63):
                i=(y*64+x)*3
                assert exports[0][i:i+3]==exports[1][i:i+3]==bytes(COLORS[2]), 'Distant defect damaged aligned lake'

    # Repaired imports still recover a colony straddling both seams.
    run(*args,'--image-seam-width',8,'--output',OUT/'protected.map','--report-file',OUT/'protected.json')
    protected=json.loads((OUT/'protected.json').read_text())['map']['colonies'][0]['start']
    assert (protected['x'],protected['y'])==(62,62)
    # Laying beaches must not undo midpoint agreement, including the four corners.
    for orientation in range(3):
        corner=[2 if ((x<32) if orientation==0 else (x>=32) if orientation==1 else (x<32 and y<32)) else 0
                for y in range(64) for x in range(64)]
        mark(corner,64,64,32,32)
        cp=OUT/f'corner-{orientation}.png';write_png(cp,64,64,corner)
        mp=OUT/f'corner-{orientation}.map.gz';ep=OUT/f'corner-{orientation}-export.png'
        run('map', 'import-image',cp,'--width',64,'--height',64,'--teams',1,'--image-seam-width',2,'--output',mp)
        run('map', 'export-image',mp,'--output',ep)
        grid=terrain_codes(ep)
        assert all(grid[y*64]==grid[y*64+63] for y in range(64)),orientation
        assert all(grid[x]==grid[63*64+x] for x in range(64)),orientation
    run('map', 'import-image',image,'--image-seam-width',17,'--output',OUT/'bad-width.map',ok=False)
    run('map', 'generate','coral','--image-seam-width',8,'--output',OUT/'bad-option.map',ok=False)
    # Stitch offset resource footprints without changing legal deposit budgets.
    resource_cells=[0]*4096
    for y in range(64):
        for x in range(64):
            if x<12 and 8<=y<16:resource_cells[y*64+x]=3
            if x>=52 and 11<=y<19:resource_cells[y*64+x]=3
            if x<12 and 44<=y<52:resource_cells[y*64+x]=4
            if x>=52 and 41<=y<49:resource_cells[y*64+x]=4
    mark(resource_cells,64,64,32,32)
    resource_image=OUT/'resource-offset.png';write_png(resource_image,64,64,resource_cells)
    reports=[]
    for width in (0,8):
        mp=OUT/f'resource-{width}.map.gz';rp=OUT/f'resource-{width}.json';ep=OUT/f'resource-{width}-export.png'
        run('map', 'import-image',resource_image,'--width',64,'--height',64,'--teams',1,
            '--image-seam-width',width,'--output',mp,'--report-file',rp)
        run('map', 'export-image',mp,'--output',ep)
        reports.append(json.loads(rp.read_text()))
        if width:
            assert reports[-1]['image_import']['seam_resource_changes']>0
            assert not reports[-1]['image_import']['resource_seam_fallback']
            _,_,px=png(ep);grid=[tuple(px[i:i+3]) for i in range(0,len(px),3)]
            assert all(grid[y*64]==grid[y*64+63] for y in range(64))
            # Protect local supplies; all changes stay within the seam strip.
            for y in range(8,56):
                for x in range(8,56):assert grid[y*64+x]==COLORS[resource_cells[y*64+x]]
    for name in ('wood','wheat'):
        assert reports[0]['resources']['types'][name]['coverage']['tiles']==reports[1]['resources']['types'][name]['coverage']['tiles']
    # Land and water budgets are independent, including their empty-cell pools.
    mixed=[2 if 36<=y<56 else 0 for y in range(64) for x in range(64)]
    for y in range(64):
        for x in range(64):
            if (x<12 and 8<=y<16) or (x>=52 and 11<=y<19):mixed[y*64+x]=3
            if (x<12 and 40<=y<44) or (x>=52 and 43<=y<47):mixed[y*64+x]=6
    mark(mixed,64,64,32,28)
    ip=OUT/'mixed-resources.png';write_png(ip,64,64,mixed)
    counts=[]
    for width in (0,8):
        rp=OUT/f'mixed-{width}.json'
        run('map', 'import-image',ip,'--width',64,'--height',64,'--teams',1,
            '--image-seam-width',width,'--output',OUT/f'mixed-{width}.map','--report-file',rp)
        r=json.loads(rp.read_text());counts.append([r['resources']['types'][name]['coverage']['tiles'] for name in ('wood','algae')])
        if width:assert not r['image_import']['resource_seam_fallback'] and r['image_import']['seam_resource_changes']>0
    assert counts[0]==counts[1],counts
    # Stable image-import state, excluding timestamp metadata, is visible after normal load.
    second=OUT/'second.map.gz'; run(*args,'--output',second,'--report-file',OUT/'second-report.json')
    second_export=OUT/'second.png'; run('map', 'export-image',second,'--output',second_export)
    assert png(second_export)==png(exported)
    loaded_report=OUT/'loaded.json'; run('map', 'preview',dest,'--report-file',loaded_report)
    loaded=json.loads(loaded_report.read_text())
    for key in ('terrain','resources'):
        assert data[key]==loaded[key],key
        assert data[key]==json.loads((OUT/'second-report.json').read_text())[key],key
    assert data['map']['colonies']==loaded['map']['colonies']
    assert data['map']['game_seed']==loaded['map']['game_seed']
    assert data['map']['colonies'][0]['start']['x']==62
    assert data['map']['colonies'][0]['start']['y']==62
    # Enlarged input decodes to the same map, including a marker crossing both seams.
    larger=[cells[(y//4)*64+x//4] for y in range(256) for x in range(256)]
    enlarged=OUT/'enlarged.png'; write_png(enlarged,256,256,larger)
    resized=OUT/'resized.map.gz'
    run('map', 'import-image',enlarged,'--width',64,'--height',64,'--teams',1,'--seed',19,'--output',resized)
    resized_export=OUT/'resized.png'; run('map', 'export-image',resized,'--output',resized_export)
    assert png(resized_export)==png(exported)
    failure=OUT/'must-not-exist.map.gz'
    failure.unlink(missing_ok=True)
    bad_teams = list(args)
    if '--teams' in bad_teams:
        index = bad_teams.index('--teams')
        del bad_teams[index:index+2]
    run(*bad_teams,'--teams',4,'--output',failure,'--report-file',OUT/'failure.json',ok=False)
    assert not failure.exists()
    assert json.loads((OUT/'failure.json').read_text())['report_type']=='image_import_failure'
    empty=OUT/'empty.png'; write_png(empty,64,64,[0]*4096)
    run('map', 'import-image',empty,'--width',64,'--height',64,'--output',failure,ok=False)
    alpha=OUT/'alpha.png'; write_png(alpha,64,64,cells,transparent=True)
    run('map', 'import-image',alpha,'--width',64,'--height',64,'--output',failure,ok=False)
    run('map', 'import-image',image,'--width',128,'--height',64,'--output',failure,ok=False)
    run('map', 'import-image',image,'--width',63,'--output',failure,ok=False)
    before=image.read_bytes()
    run('map', 'import-image',image,'--output',image,ok=False)
    assert image.read_bytes()==before
    # Centroids recover the same anchors regardless of which seam fragment is scanned first.
    for x,y in ((0,20),(1,20),(63,20),(20,0),(20,1),(20,63),(0,0),(63,63)):
        seam_cells=[0]*4096;mark(seam_cells,64,64,x,y)
        seam_image=OUT/f'seam-{x}-{y}.png';write_png(seam_image,64,64,seam_cells)
        seam_report=OUT/f'seam-{x}-{y}.json'
        run('map', 'import-image',seam_image,'--width',64,'--height',64,'--teams',1,'--output',OUT/f'seam-{x}-{y}.map','--report-file',seam_report)
        colony=json.loads(seam_report.read_text())['map']['colonies'][0]
        assert (colony['start']['x'],colony['start']['y'])==(x,y),colony
    # Upsampling and maximum worker count still initialize a complete colony.
    upsampled=OUT/'upsampled.map.gz'
    clean_cells=cells.copy(); clean_cells[45*w+45]=0
    clean_image=OUT/'clean.png'; write_png(clean_image,64,64,clean_cells)
    run('map', 'import-image',clean_image,'--width',128,'--height',128,'--workers',8,'--teams',1,'--output',upsampled,'--report-file',OUT/'upsampled.json')
    assert json.loads((OUT/'upsampled.json').read_text())['map']['colonies'][0]['units']['workers']==8
    # Reject excess colonies and overlapping repairs without writing a map.
    excess=[0]*4096
    for y in (5,20,35,50):
        for x in (5,20,35,50):
            for dy in (0,1):
                for dx in (0,1): excess[(y+dy)*64+x+dx]=11
    for y in (0,1):
        for x in (0,1): excess[y*64+x]=11  # seventeenth distinct marker
    too_many=OUT/'too-many.png'; write_png(too_many,64,64,excess)
    run('map', 'import-image',too_many,'--width',64,'--height',64,'--output',failure,ok=False)
    overlap=[0]*4096
    for x in (10,15):
        for dy in (0,1):
            for dx in (0,1): overlap[(10+dy)*64+x+dx]=11
    overlapping=OUT/'overlap.png'; write_png(overlapping,64,64,overlap)
    run('map', 'import-image',overlapping,'--width',64,'--height',64,'--output',failure,ok=False)
    # The loader resolves a .gz sibling: prevent an exporter overwriting that input.
    raw_name=OUT/'palette.map'
    before=dest.read_bytes()
    run('map', 'export-image',raw_name,'--output',dest,ok=False)
    assert dest.read_bytes()==before
    # Also prevent colliding with the map writer's automatically appended suffix.
    alias=OUT/'alias.map'; companion=OUT/'alias.map.gz'
    run(*args,'--output',alias,'--report-file',companion,ok=False)
    assert not companion.exists()
    # Exercise both generated-image output and exporter dispatch.
    generated=OUT/'generated.png'; original=OUT/'generated.map.gz'
    run('map', 'generate','coral','--seed',7,'--width',128,'--height',128,'--teams',4,'--output',original,'--map-image',generated)
    copy=OUT/'generated-copy.png'; run('map', 'export-image',original,'--output',copy)
    assert png(copy)==png(generated)
    print('Map image CLI checks passed')


if __name__=='__main__': main()
