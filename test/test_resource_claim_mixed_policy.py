import unittest,sys,hashlib,struct,re
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.tournaments.claim_policy import validate_policy,mixed_pattern_digest,policy_digest,RECOVERY_V1
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_resource_claim_policy import policy
ENTRIES=((0,0,0),(1,0,1),(-9,34,1),(6,3,1),(18,-40,0),(3,1,3),(-10,10,1),(2,-30,0),(5,-62,2),(12,11,2),(-53,-18,1),(2,2,0),(51,-24,0))
def config(frame='nearest-d4-v1'):
 return policy()|RECOVERY_V1|{'source_version':5,'pattern':'illustrative258-middle13-v1','pattern_format':'mixed-v1','pattern_frame':frame,'pattern_digest':mixed_pattern_digest(ENTRIES,frame),'opportunistic_adjacent_pickup':True}
class MixedPolicy(unittest.TestCase):
 def test_asset_bytes_match_cpp_registry(self):
  text=(Path(__file__).resolve().parents[1]/'src/map/claims/ClaimMixedPatterns.h').read_text();body=text.split('Illustrative{{')[1].split('}};')[0]
  actual=tuple(tuple(map(int,m)) for m in re.findall(r'\{(-?\d+),(-?\d+),(\d+)\}',body));self.assertEqual(actual,ENTRIES)
  for frame in ('mapaxes','nearest-d4-v1'):
   expected=hashlib.sha256(('mixed-v1:'+frame+':').encode()+b''.join(struct.pack('<hhB',*e) for e in actual)).hexdigest();self.assertEqual(mixed_pattern_digest(ENTRIES,frame),expected)
  self.assertNotEqual(mixed_pattern_digest(ENTRIES,'mapaxes'),mixed_pattern_digest(ENTRIES,'nearest-d4-v1'))
 def test_strict_config_and_catalog(self):
  for frame in ('mapaxes','nearest-d4-v1'):
   p=config(frame);caps={'resource_claims_policy_config_version':5,'resource_claims_active_policies':['experimental'],'resource_claims_policy_mixed_patterns':[{'name':p['pattern'],'format':'mixed-v1','frame':frame,'digest':p['pattern_digest']}]};self.assertEqual(validate_policy(p,caps),p)
   for bad in (p|{'selection':'closest_geometric'},p|{'direct_resource_probes':False},p|{'pattern_format':'legacy'},p|{'pattern_frame':'octant'},p|{'opportunistic_adjacent_pickup':1}):
    with self.assertRaises(ValueError):validate_policy(bad,caps)
   for key in ('pattern_format','pattern_frame','opportunistic_adjacent_pickup'):
    with self.assertRaises(ValueError):validate_policy({k:v for k,v in p.items() if k!=key})
   with self.assertRaises(ValueError):validate_policy(p,caps|{'resource_claims_policy_config_version':4})
   with self.assertRaises(ValueError):validate_policy(p,caps|{'resource_claims_policy_mixed_patterns':[]})
 def test_legacy_rejects_new_fields(self):
  for version in (1,2,3,4):
   p=policy()|({'source_version':version}|RECOVERY_V1 if version>=2 else {})|({'opportunistic_adjacent_pickup':True} if version>=3 else {})|({'direct_resource_probes':False} if version==4 else {})
   before=policy_digest(p)
   for key,value in (('pattern_format','mixed-v1'),('pattern_frame','mapaxes')):
    with self.assertRaises(ValueError):validate_policy(p|{key:value})
   self.assertEqual(policy_digest(p),before)
 def test_planner_preserves_typed_identity(self):
  from tools.tournaments.resource_claim_live import factor_configs,fractional_configs
  p=config();asset={'name':p['pattern'],'digest':mixed_pattern_digest(ENTRIES,'mapaxes'),'format':'mixed-v1','frame':'mapaxes'}
  rows=factor_configs(p,[asset]);self.assertTrue(all(r['policy']['selection']=='first_available' for r in rows));self.assertTrue(any(r['policy']['pattern_frame']=='mapaxes' for r in rows))
  with self.assertRaises(ValueError):fractional_configs(p,{'pattern_frame':['mapaxes','nearest-d4-v1'],'pending_step':['wait','direct_heading'],'use_primary_gradient':[False,True]})
 def test_entry_guards(self):
  for entries in ((),((0,0,4),),((32768,0,0),),((0,0,True),)):
   with self.assertRaises(ValueError):mixed_pattern_digest(entries,'mapaxes')
if __name__=='__main__':unittest.main()
