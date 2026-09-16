#!/usr/bin/env python3
"""Verify the published report contract, analytic fixtures, and native JSON launch modes."""
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import fairness_model
BINARY = Path(sys.argv[1]).resolve()
HARNESS = Path(sys.argv[2]).resolve()
OUT = ROOT / 'artifacts/map-report'
OUT.mkdir(parents=True, exist_ok=True)
SCHEMA = json.loads((ROOT/'docs/map-generators/map-report.schema.json').read_text(encoding='utf-8'))


def contract(value, schema=SCHEMA, path='$'):
    """Check the subset used by our published schema; not a general JSON Schema validator."""
    if '$ref' in schema:
        target = SCHEMA
        for part in schema['$ref'].split('/')[1:]:
            target = target[part]
        contract(value,target,path)
    if 'anyOf' in schema:
        for variant in schema['anyOf']:
            try:
                contract(value,variant,path)
                break
            except AssertionError:
                pass
        else:
            raise AssertionError(f'{path}: no schema variant matches')
    if 'enum' in schema: assert value in schema['enum'], path
    if 'const' in schema:
        assert type(value) is type(schema['const']) and value == schema['const'], path
    if 'type' in schema:
        types = schema['type'] if isinstance(schema['type'],list) else [schema['type']]
        matches = {'object':isinstance(value,dict), 'array':isinstance(value,list),
                   'integer':type(value) is int, 'number':type(value) in (int,float),
                   'string':isinstance(value,str), 'boolean':type(value) is bool, 'null':value is None}
        assert any(matches[t] for t in types), (path,types,type(value))
    if isinstance(value,dict):
        assert set(schema.get('required',[])) <= set(value), path
        props = schema.get('properties',{})
        extra = schema.get('additionalProperties',True)
        for key, child in value.items():
            if key in props:
                contract(child,props[key],path+'.'+key)
            else:
                assert extra is not False, (path,key)
                if isinstance(extra,dict): contract(child,extra,path+'.'+key)
    if isinstance(value,list):
        if 'minItems' in schema: assert len(value) >= schema['minItems'], path
        if 'maxItems' in schema: assert len(value) <= schema['maxItems'], path
        if 'items' in schema:
            for i, child in enumerate(value): contract(child,schema['items'],f'{path}[{i}]')
    if isinstance(value,str):
        if 'minLength' in schema: assert len(value) >= schema['minLength'], path
        if 'maxLength' in schema: assert len(value) <= schema['maxLength'], path
    if type(value) in (int,float):
        assert math.isfinite(value), path
        if 'minimum' in schema: assert value >= schema['minimum'], path
        if 'maximum' in schema: assert value <= schema['maximum'], path


def contract_rejections(report):
    """Guard both root anyOf sibling validation and the bounded telemetry contract."""
    def rejected(label, mutate):
        invalid = json.loads(json.dumps(report))
        mutate(invalid)
        try:
            contract(invalid)
        except AssertionError:
            return
        raise AssertionError('Schema accepted invalid fixture: '+label)

    def telemetry(j):
        return j['generation']['telemetry']

    rejected('unexpected root property', lambda j: j.update(unexpected=True))
    rejected('map width type', lambda j: j['map'].update(width='128'))
    rejected('telemetry kind enum', lambda j: telemetry(j)['records'][0].update(kind='unknown'))
    rejected('telemetry value type', lambda j: telemetry(j)['records'][0].update(value={}))
    rejected('negative subject', lambda j: telemetry(j)['records'][0].update(subject=-1))
    rejected('negative dropped count', lambda j: telemetry(j).update(dropped_records=-1))
    rejected('empty telemetry key', lambda j: telemetry(j)['records'][0].update(key=''))
    rejected('long telemetry key', lambda j: telemetry(j)['records'][0].update(key='x'*129))
    rejected('long telemetry text', lambda j: telemetry(j)['records'][0].update(value='x'*513))
    rejected('too many records', lambda j: telemetry(j).update(records=[telemetry(j)['records'][0]]*4097))


def close(actual, expected):
    assert math.isclose(actual,expected,rel_tol=1e-11,abs_tol=1e-11), (actual,expected)


def invariants(j):
    contract(j)
    n, area = j['map']['player_slots'], j['map']['tiles']
    assert area == j['map']['width']*j['map']['height']
    for partition in ('terrain','underlying_terrain'):
        assert sum(v['tiles'] for v in j[partition].values()) == area
        close(sum(v['percent'] for v in j[partition].values()),100)
        for v in j[partition].values(): close(v['percent'],v['tiles']*100/area)
    assert sum(r['coverage']['tiles'] for r in j['resources']['types'].values()) + j['resources']['unknown_type_tiles'] == j['resources']['occupied']['tiles']
    for model in j['movement'].values():
        matrix = model['between_colonies']
        assert len(matrix) == n and all(len(row) == n for row in matrix)
        assert all(matrix[i][i] == 0 for i in range(n))
        assert model['unreachable_directed_pairs'] == sum(matrix[i][k] is None for i in range(n) for k in range(n) if i != k)
        assert sum(c['exclusive_nearest_territory_tiles'] for c in model['colonies']) + model['territory']['tied_tiles'] + model['territory']['unreachable_tiles'] == area
    for i in range(n):
        for k in range(n):
            walk=j['movement']['walking']['between_colonies'][i][k]
            swim=j['movement']['walking_and_swimming']['between_colonies'][i][k]
            if walk is not None: assert swim is not None and swim <= walk
    quality = j['canonical_quality']
    if quality['measured']:
        model, scale = quality['model'], quality['scale']
        for field, spread in (('fitness', quality['colony_fitness']),
                              ('win_probability', quality['colony_win_probability'])):
            values = [c[field] for c in quality['colonies']]
            assert spread['count'] == len(values)
            close(spread['min'],min(values))
            close(spread['max'],max(values))
            close(spread['mean'],sum(values)/len(values))
        for c in quality['colonies']:
            r = c['raw']
            i = c['team']
            walking = j['movement']['walking']['colonies'][i]
            assert r['reachable_tiles'] == walking['reachable']['tiles']
            assert r['catchment_tiles'] == walking['catchment_tiles']
            assert r['exclusive_nearest_tiles'] == walking['exclusive_nearest_territory_tiles']
            assert r['tied_nearest_tiles'] == walking['tied_nearest_territory_tiles']
            assert r['catchment_buildable_tiles'] <= r['catchment_tiles']
            assert r['catchment_fertile_grass_tiles'] <= r['catchment_grass_tiles']
            assert r['catchment_growth_enabled_grass_tiles'] <= r['catchment_grass_tiles']
            assert r['exclusive_catchment_tiles'] + r['tied_catchment_tiles'] <= r['catchment_tiles']
            assert r['wheat_and_wood_amount'] == sum(r['resources'][k]['catchment_stored_amount'] for k in ('wheat','wood'))
            bands = c['distance_bands']
            assert [b['walking_steps'] for b in bands] == [12,24,48]
            for a,b in zip(bands,bands[1:]):
                for field in ('reached_tiles','grass_tiles','buildable_tiles','fertile_grass_tiles'):
                    assert a[field] <= b[field]
                for name in a['resources']:
                    for field in ('deposit_tiles','stored_amount'):
                        assert a['resources'][name][field] <= b['resources'][name][field]
            for band in bands:
                assert band['exclusive_nearest_tiles'] + band['tied_nearest_tiles'] <= band['reached_tiles']
                assert band['fertile_grass_tiles'] <= band['grass_tiles'] <= band['reached_tiles']
                assert band['buildable_tiles'] <= band['reached_tiles']
                for access in band['resources'].values():
                    assert access['exclusive_deposit_tiles'] + access['tied_deposit_tiles'] <= access['deposit_tiles']
                    assert access['exclusive_stored_amount'] + access['tied_stored_amount'] <= access['stored_amount']
            if scale['catchment_steps'] == 24:
                assert bands[1]['reached_tiles'] == r['catchment_tiles']
                assert bands[1]['buildable_tiles'] == r['catchment_buildable_tiles']
                assert bands[1]['fertile_grass_tiles'] == r['catchment_fertile_grass_tiles']
                assert bands[1]['exclusive_nearest_tiles'] == r['exclusive_catchment_tiles']
                assert bands[1]['tied_nearest_tiles'] == r['tied_catchment_tiles']
                for name in bands[1]['resources']:
                    assert bands[1]['resources'][name]['stored_amount'] == r['resources'][name]['catchment_stored_amount']
            for access in r['resources'].values():
                assert access['exclusive_catchment_deposit_tiles'] + access['tied_catchment_deposit_tiles'] <= access['catchment_deposit_tiles']
                assert access['exclusive_catchment_stored_amount'] + access['tied_catchment_stored_amount'] <= access['catchment_stored_amount']
            for name in ('wheat','wood'):
                assert r[name+'_distance'] == r['resources'][name]['nearest_gather_distance']
            reachable = [d for k,d in enumerate(j['movement']['walking']['between_colonies'][i]) if i != k and d is not None]
            assert r['reachable_rivals'] == len(reachable)
            assert r['farthest_rival_distance'] == (max(reachable) if reachable else None)
        # Re-derive the fitted model from the report's own raw measurements and its
        # published coefficients: the engine's fitness, win probabilities and fairness
        # must be exactly what the fitting tool's definitions produce.
        measured = [fairness_model.colony_measurements(c) for c in quality['colonies']]
        # Some terms are composites the fitting tool derives across a map's colonies.
        derived = fairness_model.derived_measurements([{'measurements': m} for m in measured])
        measured = [dict(m, **extra) for m, extra in zip(measured, derived)]
        fitness = []
        for index in range(len(measured)):
            value = model['intercept']
            for term in model['terms']:
                column = [row[term['measurement']] for row in measured]
                transform = fairness_model.TRANSFORMS[term['transform']]
                value += term['coefficient'] * transform(column[index], sum(column))
            fitness.append(value)
        probability = fairness_model.softmax(fitness)
        for c, f, p in zip(quality['colonies'], fitness, probability):
            close(c['fitness'],f)
            close(c['win_probability'],p)
        close(quality['worst_fitness'],min(fitness))
        close(quality['best_fitness'],max(fitness))
        close(quality['mean_fitness'],sum(fitness)/len(fitness))
        close(quality['fairness'],fairness_model.fairness(fitness))
        close(quality['score'],quality['fairness'])
        assert 0 <= quality['fairness'] <= 1
        assert abs(sum(probability) - 1) < 1e-9
        assert scale['catchment_steps'] > 0 and scale['threat_radius'] > 0
    else:
        assert quality['colonies'] == [] and quality['score'] is None


def main():
    logs=[]
    with tempfile.TemporaryDirectory(prefix='glob2-map-report-') as temp:
        profile=Path(temp)
        prefs=profile/'preferences.txt'
        prefs.write_text('rememberUnit=1\n')
        before=prefs.read_bytes(),prefs.stat().st_mtime_ns
        env=dict(os.environ,GLOB2_USER_DIR=str(profile),SDL_VIDEODRIVER='invalid',SDL_AUDIODRIVER='invalid')
        def run(args, ok=True, executable=BINARY):
            args=list(map(str,args))
            if executable == BINARY: args[2:2]=['-d',str(ROOT)]
            result=subprocess.run([str(executable),*args],cwd=profile,env=env,capture_output=True,text=True,timeout=120)
            logs.append({'binary':str(executable),'args':args,'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
            (OUT/'commands.json').write_text(json.dumps(logs,indent=2)+'\n')
            assert result.returncode == (0 if ok else 1),logs[-1]
        run([OUT/'fixtures'],executable=HARNESS)
        fixtures={p.stem:json.loads(p.read_text(encoding='utf-8')) for p in (OUT/'fixtures').glob('*.json')}
        for j in fixtures.values():
            if j['report_type'] == 'map': invariants(j)
            else: contract(j)
        failure = fixtures['failure']
        assert 'map' not in failure and failure['generation']['selection_quality'] is None
        assert not failure['generation']['outcome']['success']
        records = failure['generation']['telemetry']['records']
        assert [type(r['value']) for r in records] == [int,float,bool,str,str]
        assert records[0]['subject'] == 0 and records[1]['subject'] is None
        assert records[3]['value'] == 'A "quoted" choice\n'
        assert records[4]['kind'] == 'fallback'
        for i in range(4):
            assert fixtures[f'invalid-{i}']['generation']['outcome']['error'] == 'invalid_request'
        assert fixtures['invalid-0']['generation']['generator'] is None
        assert fixtures['invalid-1']['generation']['parameters'] is None
        assert fixtures['invalid-1']['generation']['raw_request']['options'] == {}
        assert fixtures['invalid-2']['generation']['raw_request']['width_exponent'] == -100
        assert fixtures['invalid-3']['generation']['raw_request']['options']['unknown-option'] == 7
        grass=fixtures['grass']
        assert grass['map']['name'] == 'A "quoted" map\né'
        assert grass['terrain']['grass'] == {'tiles':4096,'percent':100}
        assert grass['resources']['occupied']['tiles'] == 2
        assert grass['resources']['types']['wood']['stored_amount'] == 3
        assert grass['resources']['types']['wheat']['stored_amount'] == 7
        assert grass['space']['buildable']['tiles'] == 4092
        assert grass['space']['build_sites_4x4'] == 4052
        assert grass['movement']['walking']['between_colonies'] == [[0,1],[1,0]]
        assert grass['start_position_euclidean_distances'] == [[0,1],[1,0]]
        assert grass['movement']['walking']['colonies'][0]['resources']['wood']['nearest_gather_cost'] == 5
        islands=fixtures['islands']
        assert islands['terrain']['water']['tiles'] == 4094
        assert islands['movement']['walking']['between_colonies'] == [[0,None],[None,0]]
        assert islands['movement']['walking_and_swimming']['between_colonies'] == [[0,2],[2,0]]
        assert fixtures['algae-ring']['movement']['walking_and_swimming']['between_colonies'] == [[0,None],[None,0]]
        assert fixtures['empty']['canonical_quality']['measured'] is False
        assert fixtures['empty']['resources']['types']['wood']['percent_of_resource_tiles'] is None
        saved, report=OUT/'maze.map', OUT/'maze.json'
        args=['--generate-map','maze','--seed','7','--width','128','--height','128','--set','cell-shape=0']
        run([*args,'--output',saved])
        original=saved.read_bytes()
        run([*args,'--output',saved,'--json',report])
        assert saved.read_bytes() == original, 'JSON changed generated map bytes'
        generated=json.loads(report.read_text(encoding='utf-8'))
        invariants(generated)
        assert generated['generation']['available'] and generated['generation']['seed'] == 7
        assert generated['generation']['parameters']['cell-shape'] == 0
        assert generated['generation']['parameters']['width'] == 128
        telemetry=generated['generation']['telemetry']
        assert telemetry['records'] and telemetry['dropped_records'] == 0 and telemetry['invalid_values'] == 0
        assert any(r['key'].startswith('maze.') for r in telemetry['records'])
        assert generated['generation']['outcome']['success']
        contract_rejections(generated)
        run([*args,'--json',OUT/'repeat.json'])
        assert report.read_bytes() == (OUT/'repeat.json').read_bytes(), 'Report is not repeatable'
        (profile/'map.cfg').write_text('seed=99\nwidth=128\nheight=128\ncell-shape=1\n')
        run(['--generate-map','maze','--config','map.cfg','--seed','7','--set','cell-shape=0','--json',OUT/'config.json'])
        assert report.read_bytes() == (OUT/'config.json').read_bytes(), 'Config precedence differs in report'
        run(['--preview-map',saved,'--json',OUT/'loaded.json'])
        loaded=json.loads((OUT/'loaded.json').read_text(encoding='utf-8'))
        invariants(loaded)
        assert loaded['generation'] == {'available':False,'parameters':None,'telemetry':None,'reason':'Map/save files do not store the complete original generator request'}
        for key in ['terrain','underlying_terrain','resources','space','fertility','canonical_quality','movement','start_position_euclidean_distances']:
            assert loaded[key] == generated[key], key+' changed on load'
        for fixture in ['team-stats/version88.game','wrapped-building/reproducer.game','entering-explorer/reproducer.game']:
            source=ROOT/'test/fixtures'/fixture
            snapshot=source.read_bytes()
            output=OUT/(source.parent.name+'.json')
            run(['--preview-map',source,'--json',output])
            invariants(json.loads(output.read_text(encoding='utf-8')))
            assert source.read_bytes() == snapshot
        failed_path = OUT/'failed-request.json'
        run(['--generate-map','symmetric-arena','--width','128','--height','256','--teams','8',
             '--json',failed_path,'--output',OUT/'must-not-exist.map'],ok=False)
        failed=json.loads(failed_path.read_text())
        contract(failed)
        assert failed['report_type'] == 'generation_failure' and 'map' not in failed
        assert failed['generation']['outcome']['error'] == 'invalid_request'
        assert failed['generation']['telemetry']['records'][-1]['kind'] == 'error'
        assert not (OUT/'must-not-exist.map').exists()
        for bad in [[*args,'--output',saved,'--json',saved],['--preview-map',saved,'--json',saved],
                    [*args,'--json',OUT],[*args,'--json'],[*args,'--json',report,'--preview-size','256'],
                    ['--generate-map','maze','--config',profile/'map.cfg','--json',profile/'map.cfg']]:
            run(bad,ok=False)
        assert saved.read_bytes() == original
        assert (prefs.read_bytes(),prefs.stat().st_mtime_ns) == before
    print('PASS map JSON: schema contract, analytic distances/resources/space, the fitted fairness model, '
          'unchanged serialized state/RNG, config provenance, repeatability, old saves and output errors')
    print('Artifacts:',OUT)


if __name__ == '__main__':
    main()
