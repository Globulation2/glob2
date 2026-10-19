"""Read-only reproduction of the observed format-header checksum offset."""
import gzip,hashlib,json,pathlib
D=pathlib.Path(__file__).resolve().parent;B=D.parent/'runtime-macos'
old=D.parents[2]/'round-004/candidate-gen004/runtime-macos/mature-t1-probeoff/final.game.gz'
raw=gzip.decompress(old.read_bytes());offset=0
def uint():
 global offset
 value=int.from_bytes(raw[offset:offset+4],'big');offset+=4;return value
def text():
 global offset
 size=uint();value=raw[offset:offset+size].decode();offset+=size;return value
name=text();major=uint();minor=uint();teams=uint();map_offset=uint();offset+=21
terrain=[text() for _ in range(uint())];definitions=[[text(),text(),text()] for _ in range(uint())];resources=[text() for _ in range(uint())]
assert (major,minor,teams,terrain,resources)==(0,179,4,['barren-terrain','path-terrain'],[])
terrain_ids={'barren-terrain':7,'path-terrain':9}
def header(version):
 value=major^version^teams
 for key in terrain:value^=((1+terrain_ids[key])*0x9e3779b9)&0xffffffff
 for key in sorted(resources):
  for byte in key.encode():value=((value^byte)*16777619)&0xffffffff
 signed=value if value<0x80000000 else value-0x100000000
 return ((value<<31)|(signed>>1))&0xffffffff
def trace(path):return {int(parts[0]):int(parts[-1]) for parts in (line.split() for line in path.read_text().splitlines())}
a=trace(B/'old179-mature-t1-probeoff/world.checksums');b=trace(B/'new180-continuation-t1/world.checksums');overlap=sorted(a.keys()&b.keys());players=4
rotations=1+teams+1+players+1+1+2
h179,h180=header(179),header(180);delta=h179^h180;projected=((delta>>rotations)|(delta<<(32-rotations)))&0xffffffff
assert overlap and all(a[t]^b[t]==projected for t in overlap)
result=dict(status='PASS-METADATA-DIAGNOSIS-NOT-SAVE-CONTINUITY-GATE',old_save_minor=179,new_save_minor=180,teams=teams,players=players,terrain_keys=terrain,required_resource_keys=resources,header179=hex(h179),header180=hex(h180),header_xor=hex(delta),game_subsequent_right_rotations=rotations,formula='Game contribution = rotr32(MapHeader179 XOR MapHeader180, 1 + teams + 1 + players + 1 + 1 + 2)',world_xor=hex(projected),overlapping_world_ticks=len(overlap),first_tick=overlap[0],last_tick=overlap[-1],all_ticks_match_predicted_xor=True,important='MapHeader uses historical arithmetic signed right shift, not ordinary unsigned rotate. Save writes current VERSION_MINOR without mutating live loaded versionMinor.',required_control='Earlier header180 checkpoint through18432 versus header180 resume17920 through18432 must have exact raw overlapping world and snapshot checksums',source_revision='4cf3933fee8cfc63d1ddab802589dac6b0ffeb2e',input_hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [old,B/'old179-mature-t1-probeoff/world.checksums',B/'new180-continuation-t1/world.checksums']})
proof=B/'MATURE-CONTINUITY.json'
if proof.exists():
 receipt=json.loads(proof.read_text());assert receipt['status']=='PASS'
 control=B/'header180-control-from17409'
 overlap_counts={}
 for name in ['world.checksums','snapshot.checksums']:
  earlier={int(line.split()[0]):line for line in (control/name).read_text().splitlines()};later={int(line.split()[0]):line for line in (B/'new180-continuation-t1'/name).read_text().splitlines()};ticks=sorted(earlier.keys()&later.keys())
  assert ticks and all(earlier[t]==later[t] for t in ticks)
  overlap_counts[name]=len(ticks)
 assert overlap_counts=={'world.checksums':512,'snapshot.checksums':513}
 final_paths=[B/n/'final.game.gz' for n in ['old179-mature-t1-probeoff','new180-continuation-t1','header180-control-from17409']]
 assert len({p.read_bytes() for p in final_paths})==1
 old_scene=trace(B/'old179-mature-t1-probeoff/snapshot.checksums');control_scene=trace(control/'snapshot.checksums')
 assert all(old_scene[t]==value for t,value in control_scene.items())
 old_world=trace(B/'old179-mature-t1-probeoff/world.checksums');control_world=trace(control/'world.checksums')
 assert len(control_world)==1023 and all(old_world[t]^value==projected for t,value in control_world.items())
 result.update(status='PASS-METADATA-DIAGNOSIS-AND-RAW-CONTINUITY',continuity_proof=str(proof),continuity_proof_sha256=hashlib.sha256(proof.read_bytes()).hexdigest(),control_method='One real tick: old179 warm17408 advances to17409 and saves180; header180 control17409→18432 compared with header180 resume17920→18432',raw_equal_overlap=overlap_counts,all_three_final_saves_exact=True,old179_vs180_snapshot_suffix_exact=True,old179_vs180_predicted_xor_rows=1023,successful_qualification_runs=7,separate_cli_rejection=str(B/'REJECTED-ZERO-TICK-SETUP.json'))
 result.pop('required_control')
(D/'HEADER-METADATA-DIAGNOSIS.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
