"""Parameter variants must preserve each frozen map, not only seed/seat labels."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class MaximaVariantPreparationTest(unittest.TestCase):
    def test_multiple_maps_keep_their_paths_and_contents(self):
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)/'before';snapshot=base/'snapshot';snapshot.mkdir(parents=True)
            files={'glob2':b'executable','MapOne.map':b'first map','MapTwo.map':b'second map'}
            payload={'parameters':[{'key':'economy.swarm_pressure_sensitivity','value':2}]}
            files['resolved.json']=json.dumps(payload).encode()
            files['resolved.strategy']=b'economy.swarm_pressure_sensitivity = 2\n'
            for name,data in files.items():(snapshot/name).write_bytes(data)
            schedule=[{'id':i+1,'map':name,'map_file':str(snapshot/(name+'.map')),'seed':17,'candidate_seat':i} for i,name in enumerate(['MapOne','MapTwo'])]
            manifest={'matches':2,'schedule':schedule,'hashes':{k:hashlib.sha256(v).hexdigest() for k,v in files.items()}}
            (base/'manifest.json').write_text(json.dumps(manifest))
            output=Path(directory)/'after'
            subprocess.run([sys.executable,str(ROOT/'tools/prepare_maxima_ffa_variant.py'),str(base),str(output),'--set','economy.swarm_pressure_sensitivity=6'],check=True,capture_output=True,text=True)
            prepared=json.loads((output/'manifest.json').read_text())
            for old,new in zip(schedule,prepared['schedule']):
                self.assertEqual(Path(old['map_file']).name,Path(new['map_file']).name)
                self.assertEqual(Path(old['map_file']).read_bytes(),Path(new['map_file']).read_bytes())
                self.assertEqual({k:v for k,v in old.items() if k!='map_file'},{k:v for k,v in new.items() if k!='map_file'})
            self.assertEqual(prepared['hashes']['glob2'],manifest['hashes']['glob2'])
            resolved=json.loads((output/'snapshot/resolved.json').read_text())
            self.assertEqual(resolved['parameters'][0]['value'],6)

if __name__=='__main__':unittest.main()
