from pathlib import Path
import json,hashlib
ROOT=Path.cwd();BASE=ROOT/'artifacts/pending-direction-wide';gs=json.load(open('/tmp/pending-generators.json'))['generators'];(BASE/'generators.json').write_text(json.dumps(gs,indent=2))
formats=[('duel',2,[1,2]),('ffa4',4,[1,2,3,4]),('2v2',4,[1,1,2,2]),('ffa3',3,[1,2,3]),('ffa6',6,list(range(1,7))),('3v3',6,[1,1,1,2,2,2]),('ffa8',8,list(range(1,9))),('4v4',8,[1]*4+[2]*4)]
mixtures=[['nicowar','econo','castor','warrush'],['nicowar'],['castor'],['warrush','econo'],['cortex','nicowar','castor','econo'],['numbi','castor','econo','nicowar'],['cabino','nicowar','warrush','castor']]
work=[]
size_overrides={'fingerprint':(128,128),'swamp':(256,128),'river':(128,256),'islands':(256,128),'central-quarry':(128,128),'savannah':(128,128),'ring-world':(512,256),'continents':(512,512),'even-ground':(128,128)}
for i,g in enumerate(g for g in gs if not g['editorOnly']):
 name=g['id'];fmt,n,alliances=formats[i%len(formats)];w,h=size_overrides.get(name,(256,256))
 if name in ['savannah','central-quarry','even-ground']:fmt,n,alliances=formats[0]
 if name=='symmetric-arena':fmt,n,alliances='4v4',8,[1]*4+[2]*4
 if name=='continents':fmt,n,alliances=formats[7]
 if name=='concrete-islands':fmt,n,alliances=formats[2]
 if name=='bastion-keys':fmt,n,alliances=formats[0]
 mix=mixtures[i%len(mixtures)]
 if name in ['central-quarry','even-ground']:mix=['maxima','nicowar']
 players=(mix*((n+len(mix)-1)//len(mix)))[:n]
 work.append(dict(id='gen-'+name,source='generator',generator=name,width=w,height=h,teams=n,format=fmt,alliances=alliances,players=players,map_seed=713,game_seed=42,ticks=32768,controls={'width':w.bit_length()-1,'height':h.bit_length()-1,'teams':n,**({'rejoining-frequency':3} if name=='braided-delta' else {})}))
for i,m in enumerate(json.load(open(BASE/'map-inventory/index.json'))):
 n=m['teams'];alliances=list(range(1,n+1));fmt=f'ffa{n}' if n>2 else 'duel'
 if n in [4,6,8] and i%2:
  k=n//2;alliances=[1]*k+[2]*k;fmt=f'{k}v{k}'
 if n==6 and i%2==0:alliances=[1,1,2,2,3,3];fmt='2v2v2'
 if n==5 and i%2:alliances=[1,1,2,2,2];fmt='2v3'
 mix=mixtures[(i+2)%len(mixtures)];players=(mix*((n+len(mix)-1)//len(mix)))[:n]
 work.append(dict(id='map-'+m['name'].split('.map')[0],source='bundled',map_file=m['path'],map_sha256=hashlib.sha256(Path(m['path']).read_bytes()).hexdigest(),width=m['width'],height=m['height'],teams=n,format=fmt,alliances=alliances,players=players,game_seed=42,ticks=32768))
work.append(dict(id='gen-shattered-coast-8v8',source='generator',generator='shattered-coast',width=512,height=256,teams=16,format='8v8',alliances=[1]*8+[2]*8,players=['nicowar','econo','castor','warrush']*4,map_seed=713,game_seed=42,ticks=32768,controls={'width':9,'height':8,'teams':16}))
(BASE/'workloads.json').write_text(json.dumps(work,indent=2));print('Cases',len(work));print('Formats',sorted({w['format'] for w in work}));print('Sizes',sorted({(w['width'],w['height']) for w in work}));print('AIs',sorted({a for w in work for a in w['players']}))
