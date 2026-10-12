"""Job-type adapters; scheduling and transport have no experiment-specific logic."""
from pathlib import Path
import copy
import re
from .common import read_json
from .resources import Allocation, POLICY_VERSION, PROFILES, SCHEMA_VERSION, classify_cli_capability

_TYPES = {}


def register_job_type(name, adapter):
    if name in _TYPES:
        raise ValueError('job type already registered: ' + name)
    _TYPES[name] = adapter


def get_job_type(name):
    try:
        return _TYPES[name]
    except KeyError:
        raise ValueError('unknown job type: ' + name) from None


class EngineJob:
    def __init__(self, kind):
        self.kind = kind

    def validate(self, job):
        self.execution_request(job)
        config, seeds = job['config'], job['seeds']
        def valid_generator(value):
            return type(value) is int or isinstance(value,str) and bool(value) and len(value)<=128
        if isinstance(config.get('generator'),str) and ':' in config['generator'] and not any(k=='generator-package' or k.startswith('generator-package-') for k in job['inputs']):
            raise ValueError('custom generation requires frozen generator package inputs')
        generation = {'generator', 'params', 'candidates'}
        allowed = ({'players', 'ticks', 'ai_params', 'alliances', 'winning_conditions', 'rules', 'win_probability_permille', 'gpu'} | generation
                   if self.kind == 'game' else generation | {'rotations'})
        if set(config) - allowed:
            raise ValueError('unknown job configuration fields: ' + ', '.join(sorted(set(config) - allowed)))
        if self.kind == 'game' and 'save' in job['inputs'] and seeds:
            raise ValueError('saved games retain their seeds')
        if self.kind == 'game':
            if 'gpu' in config and (type(config['gpu']) is not str or config['gpu'] not in ('on', 'off')):
                raise ValueError('gpu must be on or off')
            # A game either loads a map or a save, or generates its own map inline;
            # exactly one of the three.
            inline = 'generator' in config
            sources = int('map' in job['inputs']) + int('save' in job['inputs']) + int(inline)
            if sources != 1:
                raise ValueError('game requires exactly one map, saved state or inline generator')
            if inline:
                if not valid_generator(config['generator']) or 'map' not in seeds:
                    raise ValueError('inline generation requires a map seed and generator ID')
                if not config.get('players') or 'game' not in seeds:
                    raise ValueError('new game requires players and game seed')
            if 'map' in job['inputs'] and (not config.get('players') or 'game' not in seeds):
                raise ValueError('new game requires players and game seed')
            if 'save' in job['inputs'] and any(k in config for k in ('players', 'alliances', 'ai_params', 'winning_conditions', 'rules', 'win_probability_permille')):
                raise ValueError('cannot override a saved game configuration')
            if not isinstance(config.get('rules', {}), dict) or any(
                    not isinstance(k, str) or type(v) is not int for k, v in config.get('rules', {}).items()):
                raise ValueError('rules must map names to integer values')
            if type(config.get('ticks', 90000)) is not int or config.get('ticks', 90000) < 1:
                raise ValueError('ticks must be a positive integer')
            if config.get('alliances') and len(config['alliances']) != len(config.get('players', [])):
                raise ValueError('one alliance required per player')
            permille = config.get('win_probability_permille')
            if permille is not None and (type(permille) is not int or not (permille == 0 or 501 <= permille <= 1000)):
                raise ValueError('win_probability_permille must be 0 (off) or between 501 and 1000')
        elif 'map' not in seeds or not valid_generator(config.get('generator')):
            raise ValueError('generation requires map seed and generator ID')
        if set(job['outputs']) - {'replay', 'saves', 'telemetry', 'map', 'reports', 'required', 'core', 'stack', 'result'}:
            raise ValueError('unknown requested output')
        if job['outputs'].get('result', 'full') not in ('full', 'outcome'):
            raise ValueError('result output must be full or outcome')

    def execution_request(self, job):
        if 'execution' not in job:
            return None  # Existing signed jobs retain their command and identity.
        request = job['execution']
        if self.kind != 'game':
            raise ValueError('execution profiles are not registered for map jobs')
        if (type(request) is not dict or set(request) != {
                'schema_version', 'policy_version', 'requested_execution_profile', 'compatibility_mode'}
                or type(request['schema_version']) is not int or request['schema_version'] != SCHEMA_VERSION
                or type(request['policy_version']) is not int or request['policy_version'] != POLICY_VERSION
                or request['requested_execution_profile'] not in PROFILES
                or request['compatibility_mode'] not in ('require_profile', 'legacy_argv')):
            raise ValueError('invalid execution-profile request')
        return request

    def validate_bundle_execution(self, job, bundle):
        request = self.execution_request(job)
        if request is None:
            return None
        description = bundle.get('cli_description')
        if description is None:
            if type(bundle.get('cli_version')) is not int or bundle['cli_version'] != 2:
                raise ValueError('bundle has no verified CLI2 identity')
            description = {'cli_version': 2, 'schema_version': 1, 'commands': []}
        support = classify_cli_capability(description)
        mode = request['compatibility_mode']
        if mode == 'require_profile' and support != 'execution_profiles_v1':
            raise ValueError('binary does not advertise required execution-profile contract')
        if mode == 'legacy_argv' and support != 'legacy_cli2':
            raise ValueError('legacy_argv requires explicitly selected older CLI2')
        return description

    def execution_record(self, job, bundle, attempt_dir, execution):
        request = self.execution_request(job)
        if request is None:
            if execution is not None:
                raise ValueError('allocation needs an explicit versioned execution request')
            return None
        if type(execution) is not dict:
            raise ValueError('immutable attempt execution allocation required')
        required = {'schema_version', 'policy_version', 'requested_execution_profile', 'attempt_id',
                    'slot', 'allocation', 'allocation_plan_sha256', 'measured', 'allocation_frozen',
                    'resolved_plan_status'}
        if set(execution) != required:
            raise ValueError('invalid attempt execution allocation fields')
        if (type(execution['schema_version']) is not int or execution['schema_version'] != SCHEMA_VERSION
                or type(execution['policy_version']) is not int or execution['policy_version'] != POLICY_VERSION
                or execution['requested_execution_profile'] != request['requested_execution_profile']
                or execution['attempt_id'] != Path(attempt_dir).name
                or type(execution['slot']) is not int or execution['slot'] < 0
                or type(execution['measured']) is not bool or execution['allocation_frozen'] is not True
                or execution['resolved_plan_status'] != 'pending_engine_result'
                or type(execution['allocation_plan_sha256']) is not str
                or not re.fullmatch(r'[0-9a-f]{64}', execution['allocation_plan_sha256'])):
            raise ValueError('attempt execution identity differs from request')
        a = execution['allocation']
        if type(a) is not dict or set(a) != {'cpu_ids', 'participants',
                'participants_include_simulation_owner', 'memory_planning_bytes', 'memory_enforcement'}:
            raise ValueError('invalid resource allocation')
        allocation = Allocation(a['cpu_ids'], a['participants'], a['memory_planning_bytes'])
        if a['participants_include_simulation_owner'] is not True or a['memory_enforcement'] != 'soft_planning_only':
            raise ValueError('invalid allocation ownership or memory scope')
        description = self.validate_bundle_execution(job, bundle)
        mode = request['compatibility_mode']
        if mode == 'require_profile':
            commands = [c for c in description['commands'] if type(c) is dict and c.get('path') == 'game run'
                        and c.get('available') is True]
            if len(commands) != 1 or type(commands[0].get('options')) is not list:
                raise ValueError('binary does not advertise registered game execution options')
            names = {o.get('name') for o in commands[0]['options'] if type(o) is dict and type(o.get('name')) is str}
            required_options = {'--execution-profile', '--compute-threads'}
            if allocation.memory_planning_bytes is not None:
                if allocation.memory_planning_bytes > 2**63 - 1:
                    raise ValueError('memory allocation exceeds supported CLI byte range')
                required_options.add('--memory-budget-bytes')
            if not required_options.issubset(names):
                raise ValueError('binary does not advertise assigned execution resource options')
        return {'schema_version': SCHEMA_VERSION, 'policy_version': POLICY_VERSION,
                'requested_execution_profile': request['requested_execution_profile'],
                'compatibility_mode': mode, 'allocation': allocation.record(),
                'attempt_allocation': copy.deepcopy(execution),
                'cpu_id_enforcement': 'requires_separate_worker_proof',
                'resolved_plan_status': 'legacy_not_resolved' if mode == 'legacy_argv' else 'pending_engine_result'}

    def command(self, job, bundle, attempt_dir, inputs, *, execution=None):
        if 'gpu' in job['config']:
            commands = (bundle.get('cli_description') or {}).get('commands', [])
            if not any(c.get('path') == 'game run' and c.get('available') is True
                       and type(c.get('options')) is list
                       and any(o.get('name') == '--gpu' for o in c.get('options', []) if type(o) is dict)
                       for c in commands if type(c) is dict):
                raise ValueError('binary does not advertise requested GPU option')
        record = self.execution_record(job, bundle, attempt_dir, execution)
        root = Path(bundle['directory'])
        out = Path(attempt_dir) / 'output'
        args = [str(root / bundle['executable']),
                *(['game', 'run'] if self.kind == 'game' else ['map', 'study', str(job['config']['generator'])]),
                '--output-dir', str(out), '--profile', 'glob2-tournament-' + Path(attempt_dir).name]
        config, outputs = job['config'], job['outputs']
        for key,path in sorted(inputs.items()):
            if key=='generator-package' or key.startswith('generator-package-'):
                args += ['--generator-package',str(path)]
        def add(key, value):
            args.extend([key, str(value)])
        if self.kind == 'game':
            if 'generator' in config:
                add('--generator', config['generator'])
                add('--map-seed', job['seeds']['map'])
                for key, value in sorted(config.get('params', {}).items()):
                    add('--set', f'{key}={value}')
                add('--candidates', config.get('candidates', 0))
            else:
                add('--map-file' if 'map' in inputs else '--load-game',
                    inputs.get('map', inputs.get('save')))
            if 'game' in job['seeds']:
                add('--game-seed', job['seeds']['game'])
            add('--ticks', config.get('ticks', 90000))
            if 'gpu' in config:
                add('--gpu', config['gpu'])
            for player in config.get('players', []):
                add('--player', player)
            for player, params in sorted(config.get('ai_params', {}).items()):
                for key, value in sorted(params.items()):
                    add('--ai-param', f'{player}:{key}={str(value).lower() if isinstance(value, bool) else value}')
            for group in config.get('alliances', []):
                add('--alliance', group)
            for condition in config.get('winning_conditions', []):
                add('--win-condition', condition)
            for name, value in sorted(config.get('rules', {}).items()):
                add('--rule', f'{name}={value}')
            # Ends a game once the fitted model is this sure of the result. Added
            # to the conditions in force rather than replacing them, so a real win
            # still decides the game when there is one. Off unless asked for: it
            # changes the measured outcome, so an experiment that wants the compute
            # back has to say so, and its results stay distinguishable by the
            # "win_probability" termination the engine reports.
            if config.get('win_probability_permille'):
                add('--win-probability', config['win_probability_permille'])
            for save in outputs.get('saves', []):
                add('--save', save)
            for telemetry in outputs.get('telemetry', []):
                add('--telemetry', telemetry)
            if outputs.get('replay', False): args.append('--write-replay')
        else:
            add('--seed', job['seeds']['map'])
            for key, value in sorted(config.get('params', {}).items()):
                add('--set', f'{key}={value}')
            add('--candidates', config.get('candidates', 0))
            add('--rotations', config.get('rotations', 1))
            if outputs.get('map', False): args.append('--write-map')
            for report in outputs.get('reports', []):
                add('--report', report)
        if record is not None and record['compatibility_mode'] == 'require_profile':
            args += ['--execution-profile', record['requested_execution_profile'],
                     '--compute-threads', str(record['allocation']['participants'])]
            if record['allocation']['memory_planning_bytes'] is not None:
                args += ['--memory-budget-bytes', str(record['allocation']['memory_planning_bytes'])]
        return args

    def collect(self, job, attempt_dir, *, execution=None, bundle=None):
        path = Path(attempt_dir) / 'output/result.json'
        if not path.exists():
            raise ValueError('engine did not produce result.json')
        result = read_json(path)
        if result.get('schema_version') != 1:
            raise ValueError('unsupported engine result schema')
        if result.get('status') not in ('completed', 'invalid_request', 'generation_failed', 'artifact_failure'):
            raise ValueError('invalid engine completion status')
        return self.join_execution_result(job, bundle, attempt_dir, execution, result)

    def join_execution_result(self, job, bundle, attempt_dir, execution, result):
        """Validate and join pure parsed engine output with assigned provenance."""
        request = self.execution_request(job)
        if ('gpu' in job['config'] and result.get('status') == 'completed'
                and (request is None or request['compatibility_mode'] == 'legacy_argv')):
            actual = result.get('gpu_requested')
            if type(actual) not in (int, bool) or actual != int(job['config']['gpu'] == 'on'):
                raise ValueError('engine GPU permission differs from explicit request')
        if request is not None:
            if bundle is None:
                raise ValueError('bundle capability required to collect execution result')
            record = self.execution_record(job, bundle, attempt_dir, execution)
            if record['compatibility_mode'] == 'require_profile':
                plan = result.get('execution_resources')
                if (type(plan) is not dict or type(plan.get('schema_version')) is not int
                        or plan['schema_version'] != SCHEMA_VERSION
                        or type(plan.get('policy_version')) is not int or plan['policy_version'] != POLICY_VERSION
                        or plan.get('requested_execution_profile') != record['requested_execution_profile']
                        or plan.get('selected_execution_profile') not in PROFILES
                        or plan.get('profile_source') != 'explicit_profile'
                        or type(plan.get('profile_tuning_pending')) is not bool):
                    raise ValueError('missing or invalid resolved execution-resource plan')
                if (record['requested_execution_profile'] != 'auto'
                        and plan['selected_execution_profile'] != record['requested_execution_profile']):
                    raise ValueError('selected execution profile differs from explicit request')
                resources = plan.get('resolved_resources')
                capabilities = plan.get('device_capabilities')
                if type(resources) is not dict or type(capabilities) is not dict:
                    raise ValueError('missing resolved execution resources/capabilities')
                if 'gpu' in job['config'] and result.get('status') == 'completed':
                    expected = 'enabled' if job['config']['gpu'] == 'on' else 'disabled'
                    original = plan.get('original_request')
                    overrides = original.get('resource_overrides') if type(original) is dict else None
                    sources = plan.get('resource_sources')
                    resolved = resources.get('gpu_compute_policy')
                    supported = capabilities.get('gpu_compute_supported')
                    diagnostics = plan.get('diagnostics')
                    proven_fallback = (expected == 'enabled' and resolved == 'disabled'
                        and supported is False and type(diagnostics) is list
                        and 'compute_gpu_unavailable' in diagnostics)
                    if (type(overrides) is not dict or overrides.get('gpu_compute_policy') != expected
                            or type(sources) is not dict or sources.get('gpu_compute_policy') != 'explicit_override'
                            or (resolved != expected and not proven_fallback)
                            or (resolved == 'enabled' and supported is False)):
                        raise ValueError('engine GPU plan differs from explicit request')
                    actual = result.get('gpu_requested')
                    if type(actual) not in (int, bool) or actual != int(resolved == 'enabled'):
                        raise ValueError('engine GPU permission differs from resolved plan')
                participants = resources.get('compute_participants')
                allocated = capabilities.get('allocated_cpu_participants')
                if (type(participants) is not int or participants != record['allocation']['participants']
                        or type(allocated) is not int or allocated != len(record['allocation']['cpu_ids'])):
                    raise ValueError('resolved participants or CPU allocation differs')
                for name in ('planning_memory_budget_bytes', 'recoverable_memory_budget_bytes'):
                    if name not in resources:
                        raise ValueError('missing resolved memory budget')
                    value = resources[name]
                    if (name == 'recoverable_memory_budget_bytes' and value is None) or (value is not None and (type(value) is not int or value < 0)):
                        raise ValueError('invalid resolved memory budget')
                if 'allocated_memory_bytes' not in capabilities or (capabilities['allocated_memory_bytes'] is not None
                        and (type(capabilities['allocated_memory_bytes']) is not int or capabilities['allocated_memory_bytes'] < 0)):
                    raise ValueError('invalid device memory allocation')
                actual = result.get('compute_threads')
                resolved = result.get('compute_resolved_threads')
                if (type(resolved) is not int or resolved != participants
                        or type(actual) is not int or not 1 <= actual <= participants):
                    raise ValueError('actual/frozen compute participant metadata differs from plan')
                if record['attempt_allocation']['measured'] and actual != participants:
                    raise ValueError('measured compute pool differs from frozen allocation')
                record['actual_compute_participants'] = actual
                record['runtime_participants_status'] = 'as_planned' if actual == participants else 'qualified_serial_or_partial_fallback'
                memory = record['allocation']['memory_planning_bytes']
                reported = resources.get('planning_memory_budget_bytes')
                if memory is not None and reported != memory:
                    raise ValueError('resolved planning memory differs from allocation')
                record['resolved_plan_status'] = 'reported_engine_plan'
                record['resolved_engine_plan'] = copy.deepcopy(plan)
            # Keep the engine result block unchanged; attach orchestration provenance separately.
            result['tournament_execution'] = record
        elif execution is not None:
            raise ValueError('allocation needs an explicit versioned execution request')
        return result


register_job_type('game', EngineJob('game'))
register_job_type('generate_map', EngineJob('generate_map'))
