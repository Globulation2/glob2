#!/usr/bin/env python3
"""Regenerate retained composition fixtures deliberately, never during tests."""
import copy
import json
from pathlib import Path
import random

ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).resolve().parent

def stock(family, variant=1):
    return json.loads((ROOT / 'data/buildings' / (family + '.json')).read_text())['variants'][variant]

def make(seed, missing):
    rng = random.Random(seed)
    def variant(family, key, index=1):
        v = copy.deepcopy(stock(family, index))
        v.update(key=key, previous='', next='', requiredExperiment='')
        p, s = v['properties'], v['semantics']
        p.pop('type', None)
        p.pop('shortTypeNum', None)
        p.update(level=rng.randrange(4), width=rng.randint(1, 4), height=rng.randint(1, 3))
        p['maxResource'] = [64] * 8 + [0] * 7
        s.update(repairable=False, placeable=True, instantPlacement=True)
        s['market'].update(fetchesStock=False, suppliesStock=False,
                           fetchesStockExperiment='', suppliesStockExperiment='',
                           sharedStock=False, interTeamFruitExchange=False)
        v['presentation'].update(displayName=key, skinSlot='', showLevel=False)
        return v
    refuge = variant('inn', 'refuge')
    refuge['semantics']['healing'] = copy.deepcopy(stock('hospital')['semantics']['healing'])
    refuge['semantics']['training'] = copy.deepcopy(stock('racetrack')['semantics']['training'])
    refuge['semantics']['feeding']['enabled'] = not missing
    refuge['properties']['maxUnitInside'] = 4
    refuge['semantics']['assignmentLimit'] = 6
    refuge['semantics']['regenerationPerTick'] = 1
    forge = variant('swarm', 'forge')
    forge['semantics']['production'].update(scheduling='weighted_committed_job', initialRatios=[1, 1, 1])
    forge['semantics']['production']['recipes'] = {} if missing else {
        name: dict(enabled=True, duration=rng.randint(11, 27), cost={fruit: rng.randint(1, 3)})
        for name, fruit in [('worker', 'cherry'), ('explorer', 'orange'), ('warrior', 'prune')]}
    turret = stock('defencetower')
    for key in ('shootingRange', 'shootSpeed', 'shootRhythm', 'maxBullets', 'multiplierStoneToBullets'):
        forge['properties'][key] = turret['properties'][key]
    forge['semantics'].update(projectileDamage=[3, 11, 17], projectileBuildingDamage=7)
    signal = variant('clearingflag', 'signal', 0)
    signal['properties'].update(zonable=[1, 1, 1], defaultUnitStayRange=3, maxUnitStayRange=7)
    signal['semantics'].update(assignmentLimit=9, occupiesGround=False, relocatable=True)
    warehouse = variant('market', 'warehouse')
    warehouse['semantics']['market'].update(sharedStock=True, suppliesDirectStock=True)
    warehouse['semantics']['production']['recipes'] = {}
    # Deliberately permute IDs and remove all original family names.
    variants = [refuge, forge, signal, warehouse]
    rng.shuffle(variants)
    (OUT / f'seed-{seed}.json').write_text(json.dumps({'variants': variants}, indent=2) + '\n')
    (OUT / f'seed-{seed}.manifest.json').write_text(json.dumps(dict(
        schemaVersion=1, catalogKey=f'composition-{seed}', startingBuilding='forge',
        files=[f'seed-{seed}.json']), indent=2) + '\n')

for seed, missing in [(713, False), (714, False), (715, True)]:
    make(seed, missing)
