"""Classic baselines must not be silently promoted to a claim policy."""
import copy
import gzip
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.jobs import EngineJob
from tools.tournaments.model import job
from tools.tournaments.resource_claims import ARTIFACT, trace_rows
from tools.tournaments.claim_policy import policy_digest
from claim_trace_fixture import movement_audit, stream, trace


def capture():
    rows=trace(journeys=0,movement_only=True);rows[0]['max_samples']=1;rows[-1]['live']['max_admissions']=1
    rows[-1]['movement_audit']=movement_audit((0,0,2,0,0,0),(0,0,20,0,0,0))
    return rows


class MovementOnly(unittest.TestCase):
    def request(self):
        return job('game','a'*64,inputs={'save':{'sha256':'b'*64}},config={'ticks':10,
            'resource_claims_movement_only':{'sample_every':1,'max_samples':1,'max_bytes':1048576}},
            outputs={'result':'full','required':[ARTIFACT]})

    def test_mode_is_explicit_exclusive_and_capability_gated(self):
        request=self.request();adapter=EngineJob('game');adapter.validate(request)
        bundle={'directory':'/bundle','executable':'glob2','capabilities':{'resource_claims_movement_only_version':2}}
        command=adapter.command(request,bundle,'/attempt',{'save':'/save.gz'})
        self.assertIn('--resource-claims-movement-only',command)
        self.assertNotIn('--resource-claims-shadow',command);self.assertNotIn('--resource-claims-probe',command)
        with self.assertRaisesRegex(ValueError,'bundle lacks'):
            adapter.command(request,bundle|{'capabilities':{}},'/attempt',{'save':'/save.gz'})
        for key in ('resource_claims_shadow','resource_claims_probe'):
            bad=copy.deepcopy(request);bad['config'][key]={'sample_every':1,'max_samples':1,'max_bytes':1048576}
            with self.subTest(key=key),self.assertRaises(ValueError):adapter.validate(bad)

    def test_journeys_and_claim_observation_are_rejected(self):
        self.assertEqual(len(list(trace_rows(stream(capture())))),2)
        rows=capture();rows.insert(1,{'kind':'journey'})
        with self.assertRaisesRegex(ValueError,'movement-only'):list(trace_rows(stream(rows)))
        rows=capture();rows[-1]['live']['event_counts'][2]=1
        with self.assertRaisesRegex(ValueError,'claim observation'):list(trace_rows(stream(rows)))
        rows=capture();rows[0]['movement_only']='yes'
        with self.assertRaises(ValueError):list(trace_rows(stream(rows)))

    def test_collector_binds_actual_enforcement_and_requires_segment_coverage(self):
        request=self.request();adapter=EngineJob('game')
        for policy in ('disabled','prototype-stationary-v1','experimental'):
            with self.subTest(policy=policy),tempfile.TemporaryDirectory() as directory:
                out=Path(directory)/'output';out.mkdir();rows=capture()
                result={'schema_version':1,'status':'completed','resolved':{'material_destinations_enabled':True,'active_claim_policy':policy},
                    'resource_claims_shadow':{'schema_version':2,'enforced':policy!='disabled','movement_only':True}}
                if policy=='experimental':
                    config={'source_version':1,'pattern':'spiral:k1','pattern_digest':'a'*64,'selection':'first_available',
                        'fallback':'wait','fallback_after_decisions':16,'destination_refresh_j':0,'route_refresh_j':0,
                        'route_target':'claimed_access','pending_step':'wait','maximum_expansions':4096,'use_primary_gradient':False}
                    result['resolved'].update(resource_claims_policy_config=config,resource_claims_policy_digest=policy_digest(config))
                def write():
                    (out/'result.json').write_text(json.dumps(result))
                    with gzip.open(out/ARTIFACT,'wt') as f:f.write(stream(rows).getvalue())
                write();self.assertEqual(adapter.collect(request,directory),result)
                result['resource_claims_shadow']['enforced']=policy=='disabled';write()
                with self.assertRaisesRegex(ValueError,'enforcement'):adapter.collect(request,directory)
                result['resource_claims_shadow']['enforced']=policy!='disabled'
                rows[-1]['movement_audit'].update(coverage_start=None,coverage_known=False);write()
                with self.assertRaisesRegex(ValueError,'segment coverage'):adapter.collect(request,directory)


if __name__ == '__main__':unittest.main()
