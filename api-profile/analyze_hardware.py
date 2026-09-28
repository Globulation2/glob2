import collections,json,xml.etree.ElementTree as E
from pathlib import Path
p=Path(__file__).resolve().parent
cache={};metrics=collections.Counter();weighted=collections.Counter();cycle_key=None;cycles=0;events=collections.Counter();groups=collections.defaultdict(collections.Counter);leaves=collections.Counter();schema=None;rows=collections.Counter()
def value(e):
 if 'ref' in e.attrib:return cache[e.attrib['ref']]
 if e.tag=='tagged-backtrace':v=tuple(value(c) for c in e if c.tag=='frame')
 elif e.tag=='frame':v=e.attrib.get('name','?')
 else:v=(e.text or '').strip()
 if 'id' in e.attrib:cache[e.attrib['id']]=v
 return v
for event,e in E.iterparse(p/'hardware-data.xml',events=['start','end']):
 if event=='start' and e.tag=='node':
  xpath=e.attrib['xpath'];schema='cores' if 'table[5]' in xpath else 'metrics' if 'table[61]' in xpath else 'samples'
 if event!='end' or e.tag!='row':continue
 rows[schema]+=1
 # Record every nested ID: later rows may refer to nested frame/string values.
 for c in e.iter():
  if c is not e and c.tag not in ['tagged-backtrace','frame']:value(c)
 vals=[value(c) for c in e]
 if schema=='metrics':
  if vals[2]=='cycle':
   cycles=float(vals[6]);cycle_key=(vals[0],vals[1],vals[7]);metrics['Cycles']+=cycles
  else:
   assert cycle_key==(vals[0],vals[1],vals[7])
   weighted[vals[3]]+=float(vals[6])*cycles
 elif schema=='samples':
  kind=vals[3];frames=vals[4];events[kind]+=1
  category='other'
  if any('BuildingGradientSearch::resolve' in f for f in frames):category='building propagation'
  elif any('BuildingGradientSearch::begin' in f for f in frames):category='building search setup/scan'
  elif any('Map::updateGlobalGradient(Building' in f for f in frames):category='building initialization'
  elif any('Map::propagateGradient(unsigned short' in f for f in frames):category='other gradient propagation'
  groups[category][kind]+=1
  if frames:leaves[frames[0]]+=1
 e.clear()
result={'rows':dict(rows),'cycles':metrics['Cycles'],'cycle_weighted_bottleneck_fractions':{k:v/metrics['Cycles'] for k,v in weighted.items()},'sample_events':dict(events),'sample_groups':{k:dict(v) for k,v in groups.items()},'top_sample_leaf_frames':leaves.most_common(25),'note':'Bottleneck-triggered samples are not uniform CPU-time samples. Metrics cover the full process. No direct L1/L2 cache-miss counts were recorded in this guided bottleneck configuration.'}
(p/'hardware-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
