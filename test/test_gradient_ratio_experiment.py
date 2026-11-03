"""Independent-seed planning, required evidence, and statistical unit contracts."""
import copy,json,sqlite3
import unittest
import tempfile,time,io
from pathlib import Path
from unittest.mock import patch
from collections import Counter, defaultdict
from tools.gradient_ratio_experiment import install_export_optimization, continuation_cpu_ids, ARMS, NAMED, compare, plan, add_observer_calibration, finalists, _resume_pilot, _continue_pilot, diagnostic_projection, latest_gameplay
from tools.tournaments.jobs import EngineJob, routing_ratio_outputs


def bundle():
    names=list(NAMED)+['isles','river','hills']
    return dict(id='a'*64,capabilities=dict(ais=[dict(name='maxima'),dict(name='castor')],generators=[
        dict(id=name,method=i,controls=[dict(id=key,values=values,searchValues=values) for key,values in
            [('width',[7,8,9]),('height',[7,8,9]),('teams',[2,3,4,6]),('workers',[4]),('water',[10,30])]]) for i,name in enumerate(names)]))

class RatioExperiment(unittest.TestCase):
    def test_export_optimization_repairs_missing_files(self):
        from tools.tournaments.coordinator import Coordinator
        original=Coordinator.repair_exports;original_receive=Coordinator.receive_artifact
        try:
            install_export_optimization()
            with tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp)
                for name in ('attempts','failures','results'):(root/name).mkdir()
                db=sqlite3.connect(':memory:');db.row_factory=sqlite3.Row
                db.execute('create table attempts(id,record,category)');db.execute('create table jobs(id,accepted)')
                for identity,category in [('a','success'),('b','timeout')]:
                    db.execute('insert into attempts values(?,?,?)',(identity,json.dumps({'category':category}),category))
                db.execute("insert into jobs values('job','a')")
                fake=type('Fake',(),{'root':root,'db':db})()
                Coordinator.repair_exports(fake)
                self.assertTrue((root/'failures/b.json').exists())
                self.assertFalse((root/'failures/a.json').exists())
                (root/'attempts/a.json').unlink();(root/'results/job.json').unlink()
                Coordinator.repair_exports(fake)
                self.assertEqual(json.loads((root/'attempts/a.json').read_text()),{'category':'success'})
                self.assertTrue((root/'results/job.json').exists());db.close()
        finally:
            Coordinator.repair_exports=original;Coordinator.receive_artifact=original_receive

    def test_existing_object_fast_path_checks_corruption(self):
        import hashlib
        from tools.tournaments.coordinator import Coordinator
        original=Coordinator.repair_exports;original_receive=Coordinator.receive_artifact
        try:
            install_export_optimization()
            with tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);(root/'artifacts').mkdir()
                raw=b'complete measurement';identity=hashlib.sha256(raw).hexdigest()
                target=root/'artifacts'/identity;target.write_bytes(raw)
                fake=type('Fake',(),{'root':root})()
                artifact={'sha256':identity,'bytes':len(raw)}
                self.assertTrue(Coordinator.receive_artifact(fake,None,artifact))
                target.write_bytes(b'x'*len(raw))
                with self.assertRaises(ValueError):Coordinator.receive_artifact(fake,None,artifact)
        finally:
            Coordinator.repair_exports=original;Coordinator.receive_artifact=original_receive

    def test_existing_confirmation_and_diagnostic_manifests_are_reused(self):
        from tools.gradient_ratio_experiment import _finish_campaign
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);now=time.time()
            for name in ('screening','confirmation','diagnostic-confirmation'):(root/name).mkdir()
            screen={'jobs':[{'limits':{'deadline_unix':now+100}}]}
            confirmation={'jobs':[{'limits':{'deadline_unix':now+5000}}],'sentinel':'frozen performance'}
            diagnostic={'jobs':[{'limits':{'deadline_unix':now+9000}}],'sentinel':'frozen diagnostics'}
            for name,value in [('screening/experiment.json',screen),('screening-report.json',{}),('confirmation/experiment.json',confirmation),('diagnostic-confirmation/experiment.json',diagnostic)]:
                (root/name).write_text(json.dumps(value))
            for name in ('confirmation','diagnostic-confirmation'):(root/name/'state.sqlite').touch()
            pilot={'games':[{'arm':'baseline','high_diversity_with_extra_builds':1,'wall_seconds':100,'diagnostic':False}],'stages':{}}
            with patch('tools.gradient_ratio_experiment.Coordinator') as coordinator,patch('tools.gradient_ratio_experiment.phase_job_counts',return_value={'active':1}),patch('tools.gradient_ratio_experiment.diagnostic_projection',return_value={'light_seconds':100}),patch('tools.gradient_ratio_experiment.finalists',return_value=['baseline','mode','equal']),patch('tools.gradient_ratio_experiment.run_phase',side_effect=[root/'confirmation',root/'diagnostic-confirmation']) as run,patch('tools.gradient_ratio_experiment.summarize'),patch('tools.gradient_ratio_experiment.plan') as planner:
                coordinator.return_value.status.return_value={'jobs':{}}
                coordinator.return_value.mode='paused'
                _finish_campaign(root,'/bundle',bundle(),list(range(32)),pilot,now-1000,now+10000,now+9000)
                self.assertEqual(run.call_args_list[0].args[2],confirmation)
                self.assertEqual(run.call_args_list[1].args[2],diagnostic)
                self.assertEqual(run.call_args_list[0].args[-1],now+5000)
                planner.assert_not_called()

    def test_median_amendment_keeps_original_cohort_separate(self):
        from tools.gradient_ratio_experiment import _finish_campaign
        from tools.tournaments.common import digest
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);now=time.time();arms=['baseline','mode','median']
            for name in ('screening','confirmation'):(root/name).mkdir()
            screen={'jobs':[{'limits':{'deadline_unix':now+100}}]}
            prior={'jobs':[{'limits':{'deadline_unix':now+5000}}]}
            def manifest(name,deadline):return {'metadata':{'phase':name},'jobs':[{'labels':{'variant':a},'limits':{'deadline_unix':deadline}} for a in arms]}
            new=manifest('confirmation-median',now+5000);diag=manifest('diagnostic-confirmation-median',now+9000)
            amendment={'original_deadline':now+10000,'screening_source':digest({}),'arms':arms,'performance_manifest':'new.json','diagnostic_manifest':'diag.json'}
            for name,value in [('screening/experiment.json',screen),('screening-report.json',{}),('confirmation/experiment.json',prior),('confirmation-amendment.json',amendment),('new.json',new),('diag.json',diag)]:
                (root/name).write_text(json.dumps(value))
            (root/'confirmation/state.sqlite').touch()
            pilot={'games':[{'arm':'baseline','high_diversity_with_extra_builds':1,'wall_seconds':100,'diagnostic':False}],'stages':{}}
            paths=[root/'confirmation-median',root/'diagnostic-confirmation-median']
            with patch('tools.gradient_ratio_experiment.start_supplemental_drain') as drain,patch('tools.gradient_ratio_experiment.Coordinator') as coordinator,patch('tools.gradient_ratio_experiment.phase_job_counts',return_value={'active':1}),patch('tools.gradient_ratio_experiment.diagnostic_projection',return_value={'light_seconds':100}),patch('tools.gradient_ratio_experiment.finalists',return_value=['baseline','mode','equal']),patch('tools.gradient_ratio_experiment.run_phase',side_effect=paths) as run,patch('tools.gradient_ratio_experiment.summarize') as summarize,patch('tools.gradient_ratio_experiment.plan') as planner:
                coordinator.return_value.status.return_value={'jobs':{}}
                coordinator.return_value.mode='draining';drain.return_value.wait.return_value=0
                _finish_campaign(root,'/bundle',bundle(),list(range(32)),pilot,now-1000,now+10000,now+9000)
                self.assertEqual([x.args[2] for x in run.call_args_list],[new,diag]);drain.assert_called_once()
                self.assertEqual([x.args[1].name for x in summarize.call_args_list],['confirmation-median-report.json','diagnostic-confirmation-median-report.json'])
                coordinator.return_value.control.assert_not_called()
                planner.assert_not_called()

    def test_parallel_supplemental_collection_refuses_running_admission(self):
        from tools.gradient_ratio_experiment import drain_supplemental
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'confirmation').mkdir()
            (root/'confirmation/experiment.json').write_text(json.dumps({'jobs':[]}))
            with patch('tools.gradient_ratio_experiment.Coordinator') as coordinator,patch('tools.gradient_ratio_experiment.run_phase') as run:
                coordinator.return_value.mode='running'
                with self.assertRaises(ValueError):drain_supplemental(root,'/bundle',list(range(32)))
                run.assert_not_called()

    def test_capacity_amendment_preserves_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);receipt={'cpu_ids':list(range(12,24)),'deadline':123}
            self.assertEqual(continuation_cpu_ids(root,receipt),receipt['cpu_ids'])
            record={'original_cpu_ids':receipt['cpu_ids'],'original_deadline':123,'cpu_ids':list(range(32))}
            (root/'cpu-allocation-amendment.json').write_text(json.dumps(record))
            self.assertEqual(continuation_cpu_ids(root,receipt),list(range(32)))
            record['original_deadline']=124;(root/'cpu-allocation-amendment.json').write_text(json.dumps(record))
            with self.assertRaises(ValueError):continuation_cpu_ids(root,receipt)

    def test_plan_is_reproducible_balanced_and_independent(self):
        first=plan(bundle(),phase='pilot');self.assertEqual(first,plan(bundle(),phase='pilot'))
        self.assertEqual(len(first['jobs']),12*5*5)
        setups=first['metadata']['setups']
        self.assertEqual(sum(s['generator_id'] in NAMED for s in setups),6)
        for setup in setups:
            size=setup['params']['width'];teams=setup['params']['teams']
            if teams>2:self.assertGreaterEqual(size,8)
            if setup['generator_id']=='bastion-keys':self.assertLessEqual(teams,(2**size//128)**2)
            if setup['generator_id']=='hilbert-river':self.assertGreaterEqual(size,8)
        self.assertEqual(set(s['generator_id'] for s in setups)&set(NAMED),set(NAMED))
        seeds=[(j['seeds']['map'],j['seeds']['game']) for j in first['jobs']]
        self.assertEqual(len(seeds),len(set(seeds)))
        counts=Counter((j['labels']['setup'],j['labels']['variant']) for j in first['jobs'] if not j['labels']['diagnostic'])
        self.assertEqual(set(counts.values()),{4})
        blocks=defaultdict(list)
        for j in first['jobs']:
            blocks[j['labels']['block']].append(j['labels']['variant'])
            self.assertEqual(j['config']['win_probability_permille'],950)
            self.assertNotIn('pair',j['labels'])
        self.assertGreater(len({tuple(v) for v in blocks.values()}),1)
        heldout=plan(bundle(),phase='confirmation')
        self.assertFalse({s['id'] for s in setups}&{s['id'] for s in heldout['metadata']['setups']})

    def test_calibration_is_separate_and_preserves_simulation_seeds(self):
        manifest=add_observer_calibration(plan(bundle(),phase='pilot'))
        calibration=[j for j in manifest['jobs'] if 'observer_mode' in j['labels']]
        self.assertEqual(len(calibration),24)
        groups=defaultdict(list)
        for j in calibration:
            self.assertTrue(j['labels']['diagnostic'])
            self.assertEqual(j['config']['ticks'],8192)
            groups[j['labels']['setup'],j['labels']['replicate']].append(j)
        for jobs in groups.values():
            self.assertEqual({j['labels']['observer_mode'] for j in jobs},{'off','counts','routes'})
            self.assertEqual(len({tuple(j['seeds'].values()) for j in jobs}),1)

    def test_observers_are_explicit_and_required_outputs_cannot_be_dropped(self):
        request=plan(bundle(),phase='pilot')['jobs'][0]
        adapter=EngineJob('game');adapter.validate(request)
        missing=copy.deepcopy(request);missing['outputs']['required']=[]
        with self.assertRaisesRegex(ValueError,'required outputs'):adapter.validate(missing)
        command=adapter.command(request,dict(directory='/bundle',executable='glob2'),'/attempt',{})
        self.assertIn('--gradient-work-aggregate',command)
        self.assertEqual(command[command.index('--benchmark-warmup')+1],'0')
        self.assertIn('--routing-ratio-study',command)
        self.assertEqual(command[command.index('--gpu')+1],'off')
        request['config']['routing_ratio']['policy']='unknown'
        with self.assertRaisesRegex(ValueError,'unknown routing'):adapter.validate(request)

    def test_invalid_mode_is_excluded_and_two_valid_alternatives_remain(self):
        report=dict(cpu_per_tick={a:dict(effect=v) for a,v in [('mode',-4),('speed-modes',-3),('median',-2),('equal',-1)]},
            games=[dict(arm=a,correctness_failures=int(a=='mode'),route_time_penalty=dict(n=10,mean=v))
                   for a,v in [('mode',0),('speed-modes',0),('median',.01),('equal',.02)]])
        self.assertEqual(finalists(report),['baseline','speed-modes','median'])

    def test_time_block_adjustment_removes_interleaved_host_load(self):
        games=[dict(setup='one',arm=arm,diagnostic=False,block=block,cpu_per_tick=100*block+value)
               for block in range(4) for arm,value in [('baseline',10),('mode',8)]]
        result=compare(games,draws=100)['mode']
        self.assertEqual(result['effect'],-2)
        self.assertEqual(result['ci95'],[-2,-2])

    def test_recording_recovery_preserves_original_experiment_and_deadline(self):
        from tools.tournaments.common import atomic_json
        original=plan(bundle(),phase='pilot',deadline=time.time()+18000)
        old=copy.deepcopy(original);started=time.time()-600;deadline=started+48*3600
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'pilot').mkdir()
            atomic_json(root/'pilot/experiment.json',original)
            atomic_json(root/'campaign.json',dict(cpu_ids=list(range(12,24)),started=started,deadline=deadline))
            with patch('tools.gradient_ratio_experiment.os.sched_setaffinity'),patch('tools.gradient_ratio_experiment.inspect_bundle',return_value=bundle()),patch('tools.gradient_ratio_experiment.run_phase',return_value=root/'pilot-recovery') as run,patch('tools.gradient_ratio_experiment.summarize',return_value={'games':[]}),patch('tools.gradient_ratio_experiment._finish_campaign') as finish:
                _resume_pilot('/bundle',root,list(range(12,24)))
            amended=run.call_args.args[2]
            self.assertEqual(run.call_args.args[4],started+6*3600)
            for before,after in zip(old['jobs'],amended['jobs']):
                self.assertEqual(before['config'],after['config'])
                self.assertEqual(before['seeds'],after['seeds'])
                self.assertEqual(dict(before['limits'],log_bytes=128*1024**2),after['limits'])
                self.assertEqual(after['outputs']['log_mode'],'research')
                self.assertNotEqual(before['id'],after['id'])
            self.assertEqual(finish.call_args.args[-3:-1],(started,deadline))

    def test_expensive_observer_is_selected_by_whole_arm_blocks(self):
        manifest=plan(bundle(),phase='confirmation',diagnostics=1,quality_fraction=.2,process_attempts=1)
        blocks=defaultdict(list)
        for request in manifest['jobs']:
            blocks[request['labels']['block']].append(request['config']['routing_ratio']['quality'])
            self.assertTrue(request['config']['routing_ratio']['kernels'])
            self.assertEqual(request['config']['routing_ratio']['study'],'routes')
        self.assertEqual(sum(all(v) for v in blocks.values()),round(len(blocks)*.2))
        self.assertTrue(all(len(set(v))==1 for v in blocks.values()))
        self.assertEqual(manifest['settings']['process_attempts'],1)

    def test_diagnostic_projection_accounts_for_censored_timeouts(self):
        records=[dict(started=0,finished=seconds,category=category,job=dict(labels=dict(diagnostic=True)))
                 for seconds,category in [(10,'success'),(1800,'timeout')]]
        with patch('tools.gradient_ratio_experiment.Results') as results:
            results.return_value.attempts.return_value=iter(records)
            projection=diagnostic_projection(Path('/unused'),dict(games=[dict(diagnostic=False,wall_seconds=75)]))
        self.assertGreater(projection['heavy_seconds'],1000)
        self.assertEqual(projection['heavy_attempts'],2)
        self.assertLess(projection['screen_diagnostic_job_fraction'],.2)

    def test_controller_handoff_preserves_the_current_pilot_manifest(self):
        from tools.tournaments.common import atomic_json
        started=time.time()-3600;end=started+48*3600
        manifest=plan(bundle(),phase='pilot',deadline=started+6*3600)
        manifest['metadata']['phase']='pilot-recovery'
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'pilot-recovery').mkdir()
            atomic_json(root/'campaign.json',dict(started=started,deadline=end,cpu_ids=list(range(12,24))))
            atomic_json(root/'pilot-recovery/experiment.json',manifest)
            with patch('tools.gradient_ratio_experiment.os.sched_setaffinity'),patch('tools.gradient_ratio_experiment.Coordinator'),patch('tools.gradient_ratio_experiment.inspect_bundle',return_value=bundle()),patch('tools.gradient_ratio_experiment.run_phase',return_value=root/'pilot-recovery')as run,patch('tools.gradient_ratio_experiment.summarize',return_value={'games':[]}),patch('tools.gradient_ratio_experiment._finish_campaign')as finish:
                _continue_pilot('/bundle',root,list(range(12,24)))
            self.assertEqual(run.call_args.args[2],manifest)
            self.assertEqual(run.call_args.args[4],started+6*3600)
            self.assertEqual(finish.call_args.args[-3:-1],(started,end))

    def test_fast_final_counter_reader_matches_streamed_selection(self):
        from tools.tournaments.game_telemetry import parse
        text=('GLOB2_MEASURE team=1 tick=512 counter=10\n'
              'GLOB2_MEASURE team=0 tick=256 counter=7\n'
              'GLOB2_MEASURE_HISTORY team=1 tick=999 counter=999\n'
              'GLOB2_MEASURE team=1 tick=128 counter=4\n'
              'GLOB2_MEASURE tick=768 team=0 counter=11\n'
              'GLOB2_MEASURE team=1 tick=512 final=1 counter=12\n'
              'GLOB2_PERF_SAMPLE tick=9999 calls=10\n')
        class Source:
            def open_artifact(self,record,name):return io.StringIO(text)
        expected={}
        for line in text.splitlines():
            result=parse(line)
            if result and result[0]=='GLOB2_MEASURE':
                values=result[1];team=values['team']
                if values['tick']>=expected.get(team,{}).get('tick',-1):expected[team]=values
        self.assertEqual(latest_gameplay(Source(),dict(artifacts=[dict(path='stdout.log')])),expected)
        self.assertEqual(latest_gameplay(Source(),dict(artifacts=[])),{})

    def test_completed_pilot_continuation_does_not_reopen_admission(self):
        from tools.tournaments.common import atomic_json
        started=time.time()-6*3600;end=started+48*3600
        manifest=plan(bundle(),phase='pilot')
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'pilot-recovery').mkdir()
            atomic_json(root/'campaign.json',dict(started=started,deadline=end,cpu_ids=list(range(12,24))))
            atomic_json(root/'pilot-recovery/experiment.json',manifest)
            with patch('tools.gradient_ratio_experiment.os.sched_setaffinity'),patch('tools.gradient_ratio_experiment.Coordinator')as coordinator,patch('tools.gradient_ratio_experiment.inspect_bundle',return_value=bundle()),patch('tools.gradient_ratio_experiment.run_phase')as run,patch('tools.gradient_ratio_experiment.summarize',return_value={'games':[]}),patch('tools.gradient_ratio_experiment._finish_campaign'):
                coordinator.return_value.status.return_value={'jobs':{'completed':10,'cancelled':20}}
                _continue_pilot('/bundle',root,list(range(12,24)))
                run.assert_not_called();coordinator.return_value.control.assert_not_called()

    def test_replication_cannot_be_sacrificed_to_more_setups(self):
        with self.assertRaises(ValueError):plan(bundle(),phase='pilot',replicates=3)
        with self.assertRaises(ValueError):plan(bundle(),phase='pilot',setups=3)

    def test_setup_weight_and_game_replication_are_distinct_from_tick_counts(self):
        games=[]
        for setup in ('small','large'):
            for arm,value in [('baseline',10),('mode',8)]:
                for _ in range(4 if setup=='small' else 20):
                    games.append(dict(setup=setup,arm=arm,diagnostic=False,cpu_per_tick=value))
        result=compare(games,draws=100)['mode']
        self.assertEqual(result['effect'],-2)
        self.assertEqual(result['ci95'],[-2,-2])
        self.assertEqual(result['setups'],2)
        games.extend(dict(setup='small',arm='mode',diagnostic=True,cpu_per_tick=1000) for _ in range(100))
        self.assertEqual(compare(games,draws=100)['mode']['effect'],-2)

if __name__=='__main__':unittest.main()
