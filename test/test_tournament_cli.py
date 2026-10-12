"""Pure caller allocation/CLI contracts; no workers or engine processes."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.cli import configure_hosts, parse_cpu_ids, positive_integer
from tools.tournaments.local import execute_jobs, run_job
from tools.tournaments.model import job


class TournamentCliBudgets(unittest.TestCase):
    def test_exact_cpu_ids_and_budget_numbers(self):
        self.assertEqual(parse_cpu_ids('12,3,0'), [12,3,0])
        for value in ('', '1,1', '-1', '1,', '1-3', '1, 2', '１'):
            with self.subTest(value=value), self.assertRaises(ValueError): parse_cpu_ids(value)
        self.assertEqual(positive_integer(str((1 << 63) - 1)), (1 << 63) - 1)
        for value in (True, 0, -1, '1x', '-2', (1 << 63)):
            with self.subTest(value=value), self.assertRaises(ValueError): positive_integer(value)

    def test_caller_budget_snapshot_does_not_claim_whole_host(self):
        hosts=[{'name':'local','slots':2,'transport':'local','cpu_ids':[7,2,5,1]}]
        old=copy.deepcopy(hosts)
        result=configure_hosts(hosts, memory_planning_budget_bytes=1025, resource_aware=True)
        self.assertEqual(hosts,old)
        self.assertEqual(result[0]['cpu_ids'],[7,2,5,1])
        self.assertEqual(result[0]['memory_planning_budget_bytes'],1025)
        for hosts in ([{'slots':2}], [{'cpu_ids':[1,2]}], [{'slots':3,'cpu_ids':[1,2]}]):
            with self.subTest(hosts=hosts), self.assertRaises(ValueError): configure_hosts(hosts,resource_aware=True)

    def test_multiple_hosts_need_per_host_budgets(self):
        hosts=[{'name':'a','slots':1,'cpu_ids':[2]}, {'name':'b','slots':1,'cpu_ids':[2]}]
        result=configure_hosts(hosts,resource_aware=True)
        self.assertEqual([h['cpu_ids'] for h in result],[[2],[2]])
        self.assertTrue(all(h['resource_identity_scope']=='ssh_alias_machine_identity_unverified' for h in result))
        self.assertTrue(all('resource_identity_scope' not in h for h in hosts))
        with self.assertRaises(ValueError): configure_hosts(hosts,cpu_ids=[2])
        with self.assertRaises(ValueError): configure_hosts([{'slots':1}],memory_planning_budget_bytes=10)

    def test_explicit_unmigrated_jobs_keep_original_shape(self):
        legacy=job('game','a'*64,execution=None)
        frozen=copy.deepcopy(legacy)
        self.assertNotIn('execution',legacy)
        self.assertEqual(configure_hosts([{'slots':1}],resource_aware=False),[{'slots':1}])
        self.assertEqual(legacy,frozen)
        current=job('game','a'*64)
        self.assertEqual(current['execution']['requested_execution_profile'],'efficient-throughput')
        self.assertEqual(current['execution']['compatibility_mode'],'require_profile')

    def test_local_rejects_missing_budget_before_submission_or_packaging(self):
        value=job('game','a'*64)
        with patch('tools.tournaments.local.Coordinator.submit') as submit:
            with self.assertRaises(ValueError): execute_jobs('/unused',[value],'/bundle')
            submit.assert_not_called()
        with patch('tools.tournaments.local.supplied_binary') as bundle:
            with self.assertRaises(ValueError): run_job('/unused','/unused','/unused','game',config={},seeds={})
            bundle.assert_not_called()
            with self.assertRaises(ValueError): run_job('/unused','/unused','/unused','game',config={},seeds={},cpu_ids=[1],compatibility_mode='automatic_fallback')
            bundle.assert_not_called()

    def test_local_passes_exact_host_and_shared_budget_without_mutating_jobs(self):
        value=job('game','a'*64); old=copy.deepcopy(value)
        coordinator=Mock(); coordinator.run.return_value={'jobs':{'completed':1}}
        with tempfile.TemporaryDirectory() as tmp, \
             patch('tools.tournaments.local.Coordinator.submit',return_value=coordinator), \
             patch('tools.tournaments.local.Transport') as transport, \
             patch('tools.tournaments.local.Results',return_value='results'):
            result=execute_jobs(tmp,[value],'/bundle',slots=2,cpu_ids=[8,9,12,13],memory_planning_budget_bytes=1001)
            self.assertEqual(result,'results')
            host=coordinator.run.call_args.args[0][0]
            self.assertEqual(host['slots'],2)
            self.assertEqual(host['cpu_ids'],[8,9,12,13])
            self.assertEqual(host['memory_planning_budget_bytes'],1001)
            transport.return_value.rpc.assert_called_once_with('stop')
            coordinator.close.assert_called_once()
        self.assertEqual(value,old)

    def test_benchmark_budget_and_profile_use_shared_jobs(self):
        from tools.tournaments import benchmark
        with tempfile.TemporaryDirectory() as tmp, patch.object(sys,'argv',['benchmark','--map','x','--matchup','nicowar,nicowar',
                '--games','1','--cpu-ids','2,7','--jobs','1','--execution-profile','eco','--out',tmp]), \
             patch.object(benchmark,'supplied_binary',return_value=Path('/bundle'+'a'*64)), \
             patch.object(benchmark,'store_artifact',return_value={}), \
             patch.object(benchmark,'execute_jobs',return_value=[]) as execute:
            self.assertEqual(benchmark.main(),1)
            args=execute.call_args
            self.assertEqual(args.kwargs['cpu_ids'],[2,7]); self.assertEqual(args.args[3],1)
            self.assertEqual(args.args[1][0]['execution']['requested_execution_profile'],'eco')
            self.assertEqual(args.args[1][0]['config']['players'],['nicowar','nicowar'])

    def test_fairness_uses_one_shared_coordinator_and_stable_complete_job_set(self):
        sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
        import map_fairness_tournament as fairness
        tasks=[{'map':'g','rotation':0,'game':0,'seed':19},{'map':'g','rotation':0,'game':1,'seed':20}]
        config={'ai':'nicowar','tick_cap':4096,'jobs':2,'cpu_ids':[2,3,7,8],
                'execution_profile':'efficient-throughput','execution_compatibility':'require_profile','game_timeout_seconds':60}
        records={'g':{'key':'g','colonies':2,'files':[{'rotation':0,'path':'map.gz'}]}}
        class Source:
            def __iter__(self): return iter(self.records)
            def attempts(self): return iter(())
        source=Source()
        def execute(directory,jobs,bundle,**budget):
            source.records=[{'job':job,'category':'success'} for job in jobs]
            self.assertEqual(budget['cpu_ids'],[2,3,7,8]); self.assertEqual(budget['slots'],2)
            self.assertEqual([job['seeds']['game'] for job in jobs],[19,20])
            return source
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); paths={'out':root,'games':root/'games','glob2':root/'glob2'}
            old=paths['games']/'g'/'r0-k0'/'result.json';old.parent.mkdir(parents=True);old.write_text('{"cached":true}')
            with patch.object(fairness,'supplied_binary',return_value=Path('a'*64)), \
                 patch.object(fairness,'store_artifact',return_value={}), \
                 patch.object(fairness,'execute_jobs',side_effect=execute) as execute_mock, \
                 patch.object(fairness,'run_game',return_value={'new':True}) as project:
                result=fairness.run_games(config,paths,records,tasks,lambda task,result:'done')
                self.assertEqual(result,[{'cached':True},{'new':True}]);execute_mock.assert_called_once()
                self.assertEqual(project.call_args.args[3],tasks[1])
                self.assertEqual(old.read_text(),'{"cached":true}')

    def test_native_flags_require_advertised_capabilities_and_explicit_legacy(self):
        from tools.tournaments.cli import native_execution_flags, validate_native_resource_result
        from tools.tournaments.resources import PROFILES
        description={'schema_version':1,'cli_version':2,'commands':[{'path':'game run','available':True,
            'options':[{'name':name} for name in ('--execution-profile','--compute-threads','--memory-budget-bytes')]}],
            'execution_profiles':{'policy_version':1,'resource_plan_schema_version':1,'profiles':list(PROFILES)}}
        self.assertEqual(native_execution_flags(description,'eco','require_profile',12),['--execution-profile','eco','--memory-budget-bytes','12'])
        legacy={k:v for k,v in description.items() if k!='execution_profiles'}
        self.assertEqual(native_execution_flags(legacy,'eco','legacy_argv'),[])
        for desc,mode in ((legacy,'require_profile'),(description,'legacy_argv')):
            with self.assertRaises(ValueError): native_execution_flags(desc,'eco',mode)
        receipt={'compatibility_modes':{'candidate':'require_profile'},'requested_execution_profile':'eco','cpu_ids':[2,7], 'memory_planning_bytes':12}
        result={'compute_threads':2,'compute_resolved_threads':2,'execution_resources':{'schema_version':1,'policy_version':1,'requested_execution_profile':'eco',
            'selected_execution_profile':'eco','profile_source':'explicit_profile','profile_tuning_pending':True,
            'resolved_resources':{'compute_participants':2,'planning_memory_budget_bytes':12},'device_capabilities':{'allocated_cpu_participants':2}}}
        self.assertEqual(validate_native_resource_result(result,receipt,'candidate',2),'validated_engine_resolved_plan')
        result['execution_resources']['resolved_resources']['compute_participants']=1
        with self.assertRaises(ValueError): validate_native_resource_result(result,receipt,'candidate',2)

    def test_native_prepare_checks_budget_before_any_probe(self):
        from tools.tournaments.cli import prepare_native_resources
        with patch('subprocess.run') as probe:
            with self.assertRaises(ValueError): prepare_native_resources({'baseline':'x'},None,'eco',{'baseline':'require_profile'})
            probe.assert_not_called()

    def test_release_legacy_command_keeps_original_arguments(self):
        from tools.release import benchmark_profiles
        import json
        with tempfile.TemporaryDirectory() as tmp, patch.object(benchmark_profiles.subprocess,'run') as run:
            output=Path(tmp)/'one'
            def child(command,**kwargs):
                output.joinpath('result.json').write_text(json.dumps({'setup_ns':1,'run_ns':2,'ticks':256,'termination':'tick_limit'}))
            run.side_effect=child
            benchmark_profiles.run('/binary','/fixture',output,execution_flags=[])
            self.assertEqual(run.call_args.args[0],['/binary','game','run','--load-game','/fixture','--ticks','256','--compute-threads','1','--output-dir',str(output)])

    def test_cortex_forwards_frozen_budget_without_native_execution(self):
        import importlib.util
        from types import SimpleNamespace
        root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
        spec=importlib.util.spec_from_file_location('cortex_knob_search',root/'tools/cortex-knob-search.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        args=SimpleNamespace(cpu_ids=[7,2],jobs=2,execution_profile='efficient-throughput',execution_compatibility='require_profile',memory_planning_budget_bytes=101)
        state=SimpleNamespace(data={'configs':{}});module.freeze_execution_budget(args,state)
        args.jobs=1
        with self.assertRaises(ValueError): module.freeze_execution_budget(args,state)
        with tempfile.TemporaryDirectory() as tmp, patch.object(module.subprocess,'run') as run, patch.object(module,'parse_game',return_value='W'):
            module.run_bench('/bin','/config','map',1,19,tmp,2,[7,2],'efficient-throughput','require_profile',101)
            command=run.call_args.args[0]
            self.assertEqual(command[command.index('--cpu-ids')+1],'7,2')
            self.assertEqual(command[command.index('--jobs')+1],'2')
            self.assertEqual(command[command.index('--memory-planning-budget-bytes')+1],'101')
            self.assertTrue(run.call_args.kwargs['check'])

    def test_known_local_hosts_reject_overlap_even_different_worker_roots(self):
        hosts=[{'name':'first','transport':'local','directory':'/one','slots':1,'cpu_ids':[2,7]},
               {'name':'second','transport':'local','directory':'/different','slots':1,'cpu_ids':[7,8]}]
        old=copy.deepcopy(hosts)
        with self.assertRaisesRegex(ValueError,'overlapping CPU budgets'):
            configure_hosts(hosts,resource_aware=True)
        self.assertEqual(hosts,old)
        hosts[1].pop('cpu_ids')
        with self.assertRaisesRegex(ValueError,'every local host'):
            configure_hosts(hosts,resource_aware=False)

    def test_known_local_disjoint_budgets_and_ssh_identity_qualification(self):
        hosts=[{'name':'first','transport':'local','directory':'/one','slots':1,'cpu_ids':[2,7]},
               {'name':'second','transport':'local','directory':'/two','slots':1,'cpu_ids':[3,8]},
               {'name':'remote-alias','transport':'ssh','directory':'/remote','slots':1,'cpu_ids':[2,7]}]
        old=copy.deepcopy(hosts)
        result=configure_hosts(hosts,resource_aware=True)
        self.assertEqual([h['cpu_ids'] for h in result],[[2,7],[3,8],[2,7]])
        self.assertEqual(result[0]['resource_identity_scope'],'known_local_machine_host_set_only')
        self.assertEqual(result[1]['resource_identity_scope'],'known_local_machine_host_set_only')
        self.assertEqual(result[2]['resource_identity_scope'],'ssh_alias_machine_identity_unverified')
        self.assertEqual(hosts,old)

    def test_direct_measured_result_rejects_actual_fallback_and_profile_mismatch(self):
        from tools.tournaments.cli import validate_native_resource_result
        receipt={'compatibility_modes':{'candidate':'require_profile'}, 'requested_execution_profile':'eco',
                 'cpu_ids':[2,7], 'memory_planning_bytes':None}
        good={'compute_threads':2,'compute_resolved_threads':2,'execution_resources':{
            'schema_version':1,'policy_version':1,'requested_execution_profile':'eco',
            'selected_execution_profile':'eco','profile_source':'explicit_profile','profile_tuning_pending':True,
            'resolved_resources':{'compute_participants':2},'device_capabilities':{'allocated_cpu_participants':2}}}
        self.assertEqual(validate_native_resource_result(good,receipt,'candidate',2),'validated_engine_resolved_plan')
        for field,value in [('compute_threads',1),('compute_resolved_threads',1),('compute_threads',True)]:
            bad=copy.deepcopy(good);bad[field]=value
            with self.subTest(field=field,value=value), self.assertRaises(ValueError):
                validate_native_resource_result(bad,receipt,'candidate',2)
        for field,value in [('profile_source','saved_preference'),('selected_execution_profile','luxurious'),
                            ('profile_tuning_pending',1),('schema_version',True),('policy_version',True),
                            ('requested_execution_profile','balanced')]:
            bad=copy.deepcopy(good);bad['execution_resources'][field]=value
            with self.subTest(field=field,value=value), self.assertRaises(ValueError):
                validate_native_resource_result(bad,receipt,'candidate',2)
        for container,key in [('resolved_resources','compute_participants'),('device_capabilities','allocated_cpu_participants')]:
            bad=copy.deepcopy(good);bad['execution_resources'][container][key]=True
            with self.assertRaises(ValueError):
                validate_native_resource_result(bad,receipt,'candidate',2)
        auto=copy.deepcopy(good);auto['execution_resources']['requested_execution_profile']='auto'
        auto['execution_resources']['selected_execution_profile']='balanced'
        auto_receipt=dict(receipt,requested_execution_profile='auto')
        self.assertEqual(validate_native_resource_result(auto,auto_receipt,'candidate',2),'validated_engine_resolved_plan')
        with self.assertRaises(ValueError):
            validate_native_resource_result(good,receipt,'candidate',True)

    def test_participant_override_cli_and_local_frozen_manifest(self):
        import argparse
        from tools.tournaments.cli import add_budget_arguments
        parser=argparse.ArgumentParser();add_budget_arguments(parser)
        self.assertEqual(parser.parse_args(['--compute-participants','3']).compute_participants,3)
        for value in [True,0,4]:
            with self.subTest(value=value),self.assertRaises(ValueError):
                configure_hosts([{'slots':2}],cpu_ids=[0,1,2,16,17,18],participants_per_game=value,resource_aware=True)
        value=job('game','a'*64)
        coordinator=Mock();coordinator.run.return_value={'jobs':{'completed':1}}
        with tempfile.TemporaryDirectory() as tmp, \
             patch('tools.tournaments.local.Coordinator.submit',return_value=coordinator) as submit, \
             patch('tools.tournaments.local.Transport'),patch('tools.tournaments.local.Results',return_value=[]):
            execute_jobs(tmp,[value],'/bundle',slots=2,cpu_ids=[0,1,2,16,17,18],participants_per_game=1)
            first=submit.call_args.args[1]
            self.assertEqual([a['participants'] for a in first['settings']['caller_allocation_plan']['allocations']],[1,1])
            self.assertEqual(coordinator.run.call_args.args[0][0]['participants_per_game'],1)
        with tempfile.TemporaryDirectory() as tmp, \
             patch('tools.tournaments.local.Coordinator.submit',return_value=coordinator) as submit, \
             patch('tools.tournaments.local.Transport'),patch('tools.tournaments.local.Results',return_value=[]):
            execute_jobs(tmp,[value],'/bundle',slots=2,cpu_ids=[0,1,2,16,17,18],participants_per_game=3)
            second=submit.call_args.args[1]
            self.assertNotEqual(first['id'],second['id'])
            self.assertEqual([a['cpu_ids'] for a in first['settings']['caller_allocation_plan']['allocations']],
                             [a['cpu_ids'] for a in second['settings']['caller_allocation_plan']['allocations']])


if __name__ == '__main__': unittest.main()
