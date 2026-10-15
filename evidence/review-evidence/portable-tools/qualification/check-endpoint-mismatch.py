#!/usr/bin/env python3
"""Reproduce the intentionally inconsistent both-winner endpoint fixture check.
Uses the already copied qualification tree; no games/bootstrap. Restores bytes.
"""
import importlib.util,json
from pathlib import Path
T=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('portable',T/'recompute-final.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
F=T/'qualification/relocated-bundle/evidence/confirmation/final-cohort'
p=F/'serial/complete.json';original=p.read_bytes()
try:
    fixture=json.loads(original)
    assert fixture['pairs'][0]['both_winners']
    fixture['pairs'][0]['endpoints_match']=False
    fixture['pairs'][0]['eligible_winner_timing_pair']=False
    p.write_text(json.dumps(fixture),encoding='utf-8')
    try:m.load_regime(F,'serial')
    except AssertionError:print('PASS: inconsistent both-winner endpoint pair rejected before statistics')
    else:raise RuntimeError('Inconsistent failed endpoint pair accepted')
finally:p.write_bytes(original)
