#!/usr/bin/env python3
"""Fail-closed receipt tests using an actual recorded native fixture."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import maxima_win_experiment as exp

class AuditReceipts(unittest.TestCase):
    def setUp(self):
        # Minimal complete protocol-valid receipt, including all player settings.
        self.scenario={'format':'2v2','opponent':6,'players':[
            {'player':i,'team':i,'focal':i in (1,3)} for i in range(4)]}
        self.settings={str(i):{'farming.enabled':i not in (1,3)} for i in range(4)}
        players=[]
        for i in range(4):
            values={'parameters':[{'key':'farming.enabled','value':self.settings[str(i)]['farming.enabled']}]}
            players.append({'player':i,'team':i,'allies':10 if i in (1,3) else 5,
                'ai_id':6,'implementation':'AIMaxima::Maxima','won':False,'lost':False,'alive':True,
                'requested':values,'actual':copy.deepcopy(values),'serialized_ai_checksum':12})
        self.envelope={'scenario':'identity','binary':'binary','configuration':exp.identity(self.settings)}
        self.records=[{'schema':1,'sequence':0,'type':'identity','identities':exp.canonical(self.envelope)},
            {'schema':1,'sequence':1,'type':'start','tick':0,'players':players,'world_checksum':1,'rng':'state'},
            {'schema':1,'sequence':2,'type':'terminal','tick':180000,'players':copy.deepcopy(players),
             'world_checksum':1,'rng':'state','game_ended':False,'prestige_reached':False,'termination_reason':'unfinished'}]
    def read(self,records):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'audit.jsonl';p.write_text(''.join(json.dumps(r)+'\n' for r in records))
            return exp.read_audit(p,self.envelope,self.scenario,self.settings)
    def set_disabled(self):
        for player in (1,3):
            self.settings[str(player)][exp.NO_ORDERS]=True
            for record in (self.records[1],self.records[-1]):
                record['players'][player]['orders_disabled']=True
    def test_no_orders_receipt_and_opponent_scope(self):
        self.set_disabled()
        self.assertIsNone(self.read(self.records)['outcome'])
        self.records[1]['players'][0]['orders_disabled']=True
        with self.assertRaises(exp.IntegrityError): self.read(self.records)
    def test_disabled_order_and_decision_rejected(self):
        self.set_disabled()
        for kind in ('order_issued','order_dispatched','decision'):
            records=copy.deepcopy(self.records)
            records.insert(2,{'schema':1,'type':kind,'player':1,'team':1})
            for i,r in enumerate(records): r['sequence']=i
            with self.assertRaises(exp.IntegrityError): self.read(records)
    def test_missing_disabled_receipt_rejected(self):
        self.set_disabled()
        del self.records[1]['players'][1]['orders_disabled']
        with self.assertRaises(exp.IntegrityError): self.read(self.records)
    def test_capped_game_remains_unresolved(self):
        self.assertIsNone(self.read(self.records)['outcome'])
    def test_formal_team_victory(self):
        self.records[-1]['players'][3]['won']=True
        self.assertEqual(self.read(self.records)['outcome'],1)
    def test_reject_opponent_settings_contamination(self):
        self.records[1]['players'][0]['actual']['parameters'][0]['value']=False
        with self.assertRaises(exp.IntegrityError):self.read(self.records)
    def test_reject_missing_terminal_and_sequence(self):
        with self.assertRaises(exp.IntegrityError):self.read(self.records[:-1])
        self.records[-1]['sequence']=3
        with self.assertRaises(exp.IntegrityError):self.read(self.records)
    def test_reject_ai_identity_and_team_mismatch(self):
        self.records[1]['players'][0]['implementation']='AINumbi'
        with self.assertRaises(exp.IntegrityError):self.read(self.records)
        self.records[1]['players'][0]['implementation']='AIMaxima::Maxima'
        self.records[1]['players'][0]['team']=1
        with self.assertRaises(exp.IntegrityError):self.read(self.records)
    def test_reject_decision_before_settings_receipt(self):
        self.records.insert(1,{'schema':1,'sequence':1,'type':'decision','player':0,'team':0})
        for i,r in enumerate(self.records):r['sequence']=i
        with self.assertRaises(exp.IntegrityError):self.read(self.records)
    def test_allow_load_initialization_before_receipt(self):
        self.records.insert(1,{'schema':1,'sequence':1,'type':'initialization','player':0,'team':0})
        for i,r in enumerate(self.records):r['sequence']=i
        self.assertIsNone(self.read(self.records)['outcome'])

if __name__=='__main__':unittest.main()
