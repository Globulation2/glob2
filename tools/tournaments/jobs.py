"""Job-type adapters; scheduling and transport have no experiment-specific logic."""
import csv
import gzip
import json
import re
from pathlib import Path
from .common import read_json

_TYPES = {}
_CLAIM_OBSERVERS = ('resource_claims_probe', 'resource_claims_movement_only')

def _claims_off(config):
    """True for a game set up without claims: an explicit resource_claims off,
    or material_destinations (true or false) without a claim policy."""
    if 'resource_claims_active' in config or 'resource_claims_policy' in config:
        return False
    return config.get('resource_claims') == 'off' or 'material_destinations' in config


def _claim_sampling(config):
    return next(config[key] for key in _CLAIM_OBSERVERS if key in config)

_UNIFIED_COLLECTORS=('unified-gradient-h128-v4','unified-gradient-spatial-h128-v3')
DEFAULT_GAME_TICKS = 90000
DEFAULT_WIN_PROBABILITY_PERMILLE = 950


def win_probability_threshold(config):
    """Validate and resolve a new game's threshold without changing its config."""
    rules = config.get('rules', {})
    if not isinstance(rules, dict):
        raise ValueError('rules must map names to integer values')
    values = [mapping[key] for mapping, key in
              ((config, 'win_probability_permille'), (rules, 'winProbabilityPermille'))
              if key in mapping]
    if any(type(value) is not int or not (value == 0 or 501 <= value <= 1000) for value in values):
        raise ValueError('win_probability_permille must be 0 (off) or between 501 and 1000')
    if len(values) == 2 and values[0] != values[1]:
        raise ValueError('conflicting win probability thresholds')
    return values[0] if values else DEFAULT_WIN_PROBABILITY_PERMILLE


def register_job_type(name, adapter):
    if name in _TYPES:
        raise ValueError('job type already registered: ' + name)
    _TYPES[name] = adapter


def get_job_type(name):
    try:
        return _TYPES[name]
    except KeyError:
        raise ValueError('unknown job type: ' + name) from None


def resource_gradient_collection_outputs(collection):
    """Files kept by the generic worker, including zero-row oracle streams.

    Explicit required outputs bypass the worker's outcome-only artifact pruning.
    Keep this adapter local to the tournament package so remote zip workers need
    no imports from the model fitting tools or scientific Python environment.
    """
    if collection.get('contract') in _UNIFIED_COLLECTORS:
        identity={'family','team','movement','material','key'}
        outputs={
            'gradient-episodes.csv':identity|{'tick','complete','publication_delay','queries','defer_extra','refresh_extra',
                'age','q32','q128','target_added','target_removed','target_added_frac_q12','target_removed_frac_q12',
                'since_capture_invalid_goal','since_capture_reachable_no_move','since_capture_movement_queries',
                'capture_known','movement_observed','defer_false_unreachable','refresh_false_unreachable',
                'defer_false_reachable','refresh_false_reachable','defer_stalls','refresh_stalls','defer_invalid_goals','refresh_invalid_goals',
                'defer_missed_moves','refresh_missed_moves','defer_escape_failures','refresh_escape_failures',
                'guard_balance_queries','route_changes','seed_cost_changes','planning_queries',
                'serving_capture_tick','captured_goal_count','capture_target_added_epoch','capture_target_removed_epoch',
                'capture_route_epoch','capture_seed_cost_epoch','current_target_added_epoch','current_target_removed_epoch',
                'current_route_epoch','current_seed_cost_epoch'},
            'gradient-quality.csv':identity|{'query','tick','extra_cost','false_unreachable','false_reachable','invalid_goal','missed_move','escape_failure','stall','guard_balance'},
            'gradient-planning-quality.csv':identity|{'query','tick','false_unreachable','false_reachable'},
            'gradient-contradictions.csv':identity|{'epoch','invalid_goal','reachable_no_move','movement_queries'},
        }
        if collection.get('contract')=='unified-gradient-spatial-h128-v3':
            # Historical v3 output remains readable, but current v4 never writes it.
            outputs['gradient-episodes.csv']|={'overlap_supported','overlap_support_reason','overlap_history_known','overlap_generation','overlap_publication_tick','queries_near_goal_change','queries_near_passability_change','overlap_movement_queries','queries_near_goal_added','queries_near_goal_removed','queries_near_blocked','queries_near_opened'}
        if collection.get('exploration') is not None:outputs['gradient-decisions.csv']={'tick','key','behavior_seed','behavior_interval','reason','admitted'}
        return outputs
    outputs = {
        'gradient-episodes.csv': {'tick', 'key', 'complete', 'queries', 'defer_extra',
            'refresh_extra', 'age', 'q32', 'q128', 'source_added', 'source_removed',
            'source_added_frac_q12', 'source_removed_frac_q12', 'planning_queries',
            'defer_planning_false_negative', 'defer_planning_false_positive',
            'refresh_planning_false_negative', 'refresh_planning_false_positive',
            'behavior_interval', 'behavior_seed', 'movement_observed'},
        'gradient-quality.csv': {'query', 'tick', 'extra_cost', 'false_unreachable',
                                 'false_reachable', 'flagged'},
        'gradient-planning-quality.csv': {'query', 'tick', 'false_unreachable', 'false_reachable'},
    }
    if collection.get('exploration') is not None:
        outputs['gradient-decisions.csv'] = {'tick', 'key', 'behavior_seed', 'behavior_interval',
                                              'reason', 'admitted'}
    return outputs


def _validate_resource_gradient_collection(collection):
    if not isinstance(collection, dict) or set(collection) - {'exploration', 'seed', 'threads', 'contract'}:
        raise ValueError('resource_gradient_collection must contain only exploration, seed, threads and contract')
    if 'contract' in collection and collection['contract'] not in _UNIFIED_COLLECTORS:
        raise ValueError('Unknown explicit gradient collection contract')
    if type(collection.get('seed', 0)) is not int or not 0 <= collection.get('seed', 0) <= 2**32-1:
        raise ValueError('resource gradient exploration seed must be a uint32 integer')
    if 'threads' in collection and (type(collection['threads']) is not int or not 1 <= collection['threads'] <= 64):
        raise ValueError('resource gradient collection threads must be an integer in 1..64')
    if 'exploration' in collection:
        value = collection['exploration']
        if not isinstance(value, str) or not re.fullmatch(r'[0-9]+(?:,[0-9]+)*', value):
            raise ValueError('resource gradient exploration must be comma-separated integer intervals')
        intervals = [int(part) for part in value.split(',')]
        if len(intervals) != len(set(intervals)) or any(n != 0 and not 8 <= n <= 65536 for n in intervals):
            raise ValueError('resource gradient exploration intervals must be unique: 0 or 8..65536')


class EngineJob:
    def __init__(self, kind):
        self.kind = kind

    def validate(self, job):
        config, seeds = job['config'], job['seeds']
        def valid_generator(value):
            return type(value) is int or isinstance(value,str) and bool(value) and len(value)<=128
        if isinstance(config.get('generator'),str) and ':' in config['generator'] and not any(k=='generator-package' or k.startswith('generator-package-') for k in job['inputs']):
            raise ValueError('custom generation requires frozen generator package inputs')
        generation = {'generator', 'params', 'candidates'}
        allowed = ({'players', 'ticks', 'ai_params', 'alliances', 'winning_conditions', 'rules', 'experiments', 'win_probability_permille', 'resource_gradient_collection', 'resource_claims_probe', 'resource_claims_movement_only', 'resource_claims', 'resource_claims_active', 'resource_claims_policy', 'material_destinations', 'benchmark_warmup_ticks', 'gradient_backend'} | generation
                   if self.kind == 'game' else generation | {'rotations'})
        if set(config) - allowed:
            raise ValueError('unknown job configuration fields: ' + ', '.join(sorted(set(config) - allowed)))
        if self.kind == 'game' and 'save' in job['inputs'] and seeds:
            raise ValueError('saved games retain their seeds')
        if 'benchmark_warmup_ticks' in config:
            warmup=config['benchmark_warmup_ticks'];limit=config.get('ticks',DEFAULT_GAME_TICKS)
            if type(limit) is not int or type(warmup) is not int or not 0<=warmup<=2147483647 or warmup>=limit:
                raise ValueError('benchmark_warmup_ticks must leave a positive measured window')
        if 'gradient_backend' in config and config['gradient_backend'] not in ('cpu','scalar','sse2','neon','opencl','vulkan','webgpu'):
            raise ValueError('invalid gradient_backend')
        if self.kind == 'game' and 'material_destinations' in config:
            if type(config['material_destinations']) is not bool:
                raise ValueError('material_destinations must be a boolean')
            if config['material_destinations'] and job['outputs'].get('replay'):
                raise ValueError('material destinations cannot record replay output')
        if self.kind == 'game' and 'resource_claims' in config:
            # Without this key a new game uses its pinned engine's default:
            # claims off before the shipped default, contention-v1 after it.
            if config['resource_claims'] not in ('default', 'off'):
                raise ValueError('resource_claims must be default or off')
            if config['resource_claims'] == 'default' and any(k in config for k in ('material_destinations', 'resource_claims_active', 'resource_claims_policy')):
                raise ValueError('resource_claims default excludes explicit claim setup')
            if config['resource_claims'] == 'off' and any(k in config for k in ('resource_claims_active', 'resource_claims_policy')):
                raise ValueError('resource_claims off excludes claim policies')
            if config['resource_claims'] == 'off' and job['outputs'].get('replay'):
                raise ValueError('resource_claims off cannot record replay output')
        if self.kind == 'game' and 'resource_claims_active' in config:
            if config['resource_claims_active'] != 'prototype-stationary-v1' or config.get('material_destinations') is not True:
                raise ValueError('active claims require known prototype and material destinations')
            if job['outputs'].get('replay'):
                raise ValueError('active claims exclude replay')
        if 'resource_claims_policy' in config:
            from .claim_policy import validate_policy
            validate_policy(config['resource_claims_policy'])
            if config.get('material_destinations') is not True or 'resource_claims_active' in config or job['outputs'].get('replay'):
                raise ValueError('experimental claim policy requires material destinations and excludes other modes and replay')
        if sum(key in config for key in _CLAIM_OBSERVERS) > 1:
            raise ValueError('choose exactly one claim observer')
        if 'resource_claims_probe' in config and _claims_off(config) and 'save' not in job['inputs']:
            raise ValueError('active probe requires active new game or saved state')
        if self.kind == 'game' and any(key in config for key in _CLAIM_OBSERVERS):
            claims = _claim_sampling(config)
            bounds = {'sample_every': (1, 1000000000), 'max_samples': (1, 1000000), 'max_bytes': (1024, 2147483647)}
            if not isinstance(claims, dict) or set(claims) != set(bounds):
                raise ValueError('claim observer requires sample_every, max_samples and max_bytes')
            for key, (low, high) in bounds.items():
                if type(claims[key]) is not int or not low <= claims[key] <= high:
                    raise ValueError('invalid claim observer ' + key)
            if 'resource-claims-shadow.jsonl.gz' not in job['outputs'].get('required', []):
                raise ValueError('claim observer requires its compressed JSONL artifact')
            if job['outputs'].get('result', 'full') != 'full':
                raise ValueError('claim observer requires full result provenance')
        if self.kind == 'game' and 'resource_gradient_collection' in config:
            collection = config['resource_gradient_collection']
            _validate_resource_gradient_collection(collection)
            if 'generator' in config:
                raise ValueError('resource gradient collection requires a frozen map or save input; inline generation is forbidden')
            if job['outputs'].get('replay'):
                raise ValueError('resource gradient collection cannot record replay output')
            if job['outputs'].get('result', 'full') != 'full':
                raise ValueError('resource gradient collection requires full result provenance')
            requested = job['outputs'].get('required', [])
            if not isinstance(requested, list) or any(not isinstance(name, str) for name in requested):
                raise ValueError('resource gradient collection required outputs must be a list of filenames')
            missing = resource_gradient_collection_outputs(collection).keys() - set(requested)
            if missing:
                raise ValueError('resource gradient collection requires output files: ' + ', '.join(sorted(missing)))
        if self.kind == 'game':
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
            if 'save' in job['inputs'] and any(k in config for k in ('players', 'alliances', 'ai_params', 'winning_conditions', 'rules', 'experiments', 'material_destinations', 'resource_claims', 'resource_claims_active', 'resource_claims_policy', 'win_probability_permille')):
                raise ValueError('cannot override a saved game configuration')
            experiments = config.get('experiments', [])
            if (not isinstance(experiments, list) or len(experiments) > 64 or
                    any(not isinstance(key, str) or not key or len(key) > 128 for key in experiments) or
                    len(experiments) != len(set(experiments))):
                raise ValueError('experiments must be a unique list of at most 64 nonempty experiment keys')
            if not isinstance(config.get('rules', {}), dict) or any(
                    not isinstance(k, str) or type(v) is not int for k, v in config.get('rules', {}).items()):
                raise ValueError('rules must map names to integer values')
            if type(config.get('ticks', DEFAULT_GAME_TICKS)) is not int or config.get('ticks', DEFAULT_GAME_TICKS) < 1:
                raise ValueError('ticks must be a positive integer')
            if config.get('alliances') and len(config['alliances']) != len(config.get('players', [])):
                raise ValueError('one alliance required per player')
            win_probability_threshold(config)
        elif 'map' not in seeds or not valid_generator(config.get('generator')):
            raise ValueError('generation requires map seed and generator ID')
        if set(job['outputs']) - {'replay', 'saves', 'telemetry', 'map', 'reports', 'required', 'core', 'stack', 'result', 'log_mode'}:
            raise ValueError('unknown requested output')
        if job['outputs'].get('log_mode', 'full') not in ('full', 'win-probability', 'gameplay', 'telemetry'):
            raise ValueError('log_mode must be full, telemetry, gameplay or win-probability')
        if job['outputs'].get('log_mode') == 'telemetry' and not job['outputs'].get('telemetry'):
            raise ValueError('telemetry log mode requires requested telemetry')
        if job['outputs'].get('log_mode') == 'win-probability' and (self.kind != 'game' or 'team-timeline' not in job['outputs'].get('telemetry', [])):
            raise ValueError('win-probability logs require game team-timeline telemetry')
        if job['outputs'].get('log_mode') == 'gameplay' and (self.kind != 'game' or 'team-timeline' not in job['outputs'].get('telemetry', [])):
            raise ValueError('gameplay logs require game team-timeline telemetry')
        if 'require_gameplay_telemetry' in job.get('labels', {}):
            required = job['labels']['require_gameplay_telemetry']
            if type(required) is not bool:
                raise ValueError('require_gameplay_telemetry must be a boolean')
            if required and (self.kind != 'game' or
                    'team-timeline' not in job['outputs'].get('telemetry', []) or
                    job['outputs'].get('log_mode', 'full') not in ('full', 'gameplay')):
                raise ValueError('required gameplay telemetry needs game team-timeline and full or gameplay logs')
        if 'require_movement_audit' in job.get('labels', {}):
            required = job['labels']['require_movement_audit']
            if type(required) is not bool:
                raise ValueError('require_movement_audit must be a boolean')
            if required and (self.kind != 'game' or not any(k in config for k in _CLAIM_OBSERVERS)):
                raise ValueError('required movement audit needs an explicit game claim observer')
        if ('checksums' in job['outputs'].get('telemetry', []) and config.get('ticks', DEFAULT_GAME_TICKS) > 4096
                and 'artifact_bytes' not in job['limits']):
            raise ValueError('detailed checksums for long games require an explicit artifact_bytes limit; use short focused probes')
        if job['outputs'].get('result', 'full') not in ('full', 'outcome'):
            raise ValueError('result output must be full or outcome')

    def command(self, job, bundle, attempt_dir, inputs):
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
            add('--ticks', config.get('ticks', DEFAULT_GAME_TICKS))
            if 'benchmark_warmup_ticks' in config:
                add('--benchmark-warmup',config['benchmark_warmup_ticks'])
            if 'gradient_backend' in config:
                add('--gradient-backend',config['gradient_backend'])
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
            capabilities = bundle.get('capabilities', {})
            if 'save' not in inputs:
                if config.get('resource_claims') == 'default' and 'resource_claims_default' not in capabilities:
                    raise ValueError('bundle has no default resource-claim policy')
                # Claims-off setups (explicit off, or material_destinations
                # without a policy: its meaning before the shipped default)
                # must be explicit on engines whose new games default to claims.
                if _claims_off(config) and capabilities.get('resource_claims_switch_version') == 1:
                    if outputs.get('replay', False):
                        raise ValueError('claims-off games cannot record replay output')
                    add('--resource-claims', 'off')
            if config.get('material_destinations', False):
                args.append('--material-destinations')
            for experiment in config.get('experiments', []):
                add('--experiment', experiment)
            # Planners freeze the threshold in the job. Low-level requests keep
            # CLI defaults (95% on new games); an explicit zero disables it.
            if 'win_probability_permille' in config:
                add('--win-probability', config['win_probability_permille'])
            for save in outputs.get('saves', []):
                add('--save', save)
            for telemetry in outputs.get('telemetry', []):
                add('--telemetry', telemetry)
            if outputs.get('replay', False): args.append('--write-replay')
            if 'resource_claims_active' in config:
                add('--resource-claims-active', config['resource_claims_active'])
            if 'resource_claims_policy' in config:
                from .claim_policy import canonical_policy
                add('--resource-claims-policy-json', canonical_policy(config['resource_claims_policy']))
            if any(key in config for key in _CLAIM_OBSERVERS):
                claims = _claim_sampling(config)
                observer = next(key for key in _CLAIM_OBSERVERS if key in config)
                capabilities = bundle.get('capabilities', {})
                if observer == 'resource_claims_movement_only' and capabilities.get('resource_claims_movement_only_version') != 2:
                    raise ValueError('bundle lacks movement-only observer version 2')
                if observer == 'resource_claims_probe' and capabilities.get('resource_claims_probe_version') != 3:
                    raise ValueError('bundle lacks claim probe version 3')
                args.append('--'+observer.replace('_','-'))
                for key, flag in (('sample_every', '--resource-claims-sample-every'),
                                  ('max_samples', '--resource-claims-max-samples'),
                                  ('max_bytes', '--resource-claims-max-bytes')):
                    add(flag, claims[key])
            if 'resource_gradient_collection' in config:
                collection = config['resource_gradient_collection']
                args.append('--resource-gradient-probe')
                # Match the permanent collector: host GPU/profile defaults must
                # not alter collection execution or add incidental timing work.
                add('--gpu', 'off')
                add('--compute-telemetry', 'off')
                # Non-affinity hosts need an explicit per-job participant budget.
                # Allocation-backed hosts leave this unset; their worker supplies it.
                if 'threads' in collection:
                    add('--compute-threads', collection['threads'])
                if 'exploration' in collection:
                    add('--resource-gradient-exploration', collection['exploration'])
                    add('--resource-gradient-exploration-seed', collection.get('seed', 0))
        else:
            add('--seed', job['seeds']['map'])
            for key, value in sorted(config.get('params', {}).items()):
                add('--set', f'{key}={value}')
            add('--candidates', config.get('candidates', 0))
            add('--rotations', config.get('rotations', 1))
            if outputs.get('map', False): args.append('--write-map')
            for report in outputs.get('reports', []):
                add('--report', report)
        return args

    def collect(self, job, attempt_dir):
        path = Path(attempt_dir) / 'output/result.json'
        if not path.exists():
            raise ValueError('engine did not produce result.json')
        result = read_json(path)
        if result.get('schema_version') != 1:
            raise ValueError('unsupported engine result schema')
        if result.get('status') not in ('completed', 'invalid_request', 'generation_failed', 'artifact_failure'):
            raise ValueError('invalid engine completion status')
        if result.get('status') == 'completed':
            if job.get('labels', {}).get('require_gameplay_telemetry'):
                from .game_telemetry import parse
                from .storage import DEFAULT_LOG_BYTES
                retained = 0
                found_final = False
                with gzip.open(Path(attempt_dir) / 'stdout.log.gz', 'rb') as stream:
                    while True:
                        line = stream.readline(1024**2 + 1)
                        if not line:
                            break
                        retained += len(line)
                        if len(line) > 1024**2 or retained > job['limits'].get('log_bytes', DEFAULT_LOG_BYTES):
                            raise ValueError('gameplay log exceeds collection allowance')
                        if line.startswith(b'GLOB2_MEASURE '):
                            _, fields = parse(line.decode('utf-8'))
                            if fields.get('final') == 1 and type(fields.get('tick')) is int:
                                found_final = True
                if not found_final:
                    raise ValueError('engine did not retain final gameplay telemetry')
            if 'gradient_backend' in job['config'] and result.get('gradient_backend')!=job['config']['gradient_backend']:
                raise ValueError('engine gradient backend differs from frozen request')
            if 'benchmark_warmup_ticks' in job['config']:
                fields=('benchmark_setup_cpu_ns','benchmark_run_cpu_ns','benchmark_save_cpu_ns','benchmark_measured_ticks','ticks')
                if (any(type(result.get(k)) is not int or result[k]<0 for k in fields) or
                    not result['benchmark_run_cpu_ns'] or not 0<result['benchmark_measured_ticks']<=result['ticks']-job['config']['benchmark_warmup_ticks']):
                    raise ValueError('engine did not attest a measured CPU window')
            resolved = result.get('resolved', {})
            if resolved.get('active_claim_policy') == 'experimental':
                from .claim_policy import attested_policy_digest
                attested_policy_digest(resolved)
                if ('resource_claims_policy' not in job['config'] and 'save' not in job['inputs']
                        and (_claims_off(job['config']) or resolved.get('resource_claims') != 'default')):
                    raise ValueError('engine experimental claim policy differs from frozen request')
            if job['config'].get('resource_claims') == 'default' and resolved.get('resource_claims') != 'default':
                raise ValueError('engine did not attest its default resource-claim policy')
            if _claims_off(job['config']) and 'save' not in job['inputs'] and resolved.get('active_claim_policy', 'disabled') != 'disabled':
                raise ValueError('engine claim policy differs from frozen claims-off request')
            if 'resource_claims_policy' in job['config']:
                from .claim_policy import normalized_policy, policy_digest
                policy = job['config']['resource_claims_policy']
                resolved = result.get('resolved', {})
                if resolved.get('active_claim_policy') != 'experimental' or resolved.get('resource_claims_policy_config') != normalized_policy(policy) or resolved.get('resource_claims_policy_digest') != policy_digest(policy):
                    raise ValueError('engine experimental claim policy differs from frozen request')
            if 'material_destinations' in job['config'] and result.get('resolved', {}).get('material_destinations_enabled') is not job['config']['material_destinations']:
                raise ValueError('engine material destination mode differs from frozen request')
            if 'resource_claims_active' in job['config'] and result.get('resolved', {}).get('active_claim_policy') != job['config']['resource_claims_active']:
                raise ValueError('engine active claim policy differs from frozen request')
        if result.get('status') == 'completed' and any(key in job['config'] for key in _CLAIM_OBSERVERS):
            from .resource_claims import ARTIFACT, trace_rows
            mode = result.get('resolved', {}).get('material_destinations_enabled')
            if type(mode) is not bool:
                raise ValueError('engine did not attest saved/new material destination mode')
            if 'material_destinations' in job['config'] and mode != job['config']['material_destinations']:
                raise ValueError('engine material destination mode differs from frozen request')
            attestation = result.get('resource_claims_shadow', {})
            movement_only = 'resource_claims_movement_only' in job['config']
            actual_policy = result.get('resolved', {}).get('active_claim_policy')
            if movement_only and actual_policy not in ('disabled','prototype-stationary-v1','experimental'):
                raise ValueError('movement-only observer lacks claim mode provenance')
            active = actual_policy != 'disabled' if movement_only else True
            if attestation.get('movement_only',False) is not movement_only:
                raise ValueError('engine claim observation mode differs from request')
            if attestation.get('schema_version') != 2 or attestation.get('enforced') is not active:
                raise ValueError('engine did not attest requested claim enforcement mode')
            if not movement_only:
                expected_policy = ('experimental' if 'resource_claims_policy' in job['config']
                                   else job['config'].get('resource_claims_active'))
                # Saved-state identity freezes its policy. Do not silently
                # relabel an experimental continuation as the old prototype.
                if actual_policy not in ('prototype-stationary-v1', 'experimental') or (expected_policy is not None and actual_policy != expected_policy):
                    raise ValueError('engine active claim policy differs from frozen request')
            sampling = _claim_sampling(job['config'])
            with gzip.open(path.parent / ARTIFACT, 'rt') as stream:
                rows = trace_rows(stream)
                schema = next(rows)
                if schema.get('movement_only') is not movement_only:
                    raise ValueError('movement-only schema mismatch')
                for config_key, schema_key in (('sample_every', 'sample_every'), ('max_samples', 'max_samples'), ('max_bytes', 'max_output_bytes')):
                    if sampling[config_key] != schema.get(schema_key):
                        raise ValueError('resource claim sampling limits changed')
                from .claim_journeys import attest_digests
                for row in rows:
                    if row['kind'] == 'journey':
                        attest_digests([row['policy_digest']], result.get('resolved', {}))
                if (movement_only or job.get('labels', {}).get('require_movement_audit')) and row['movement_audit']['coverage_known'] is not True:
                    raise ValueError('required movement audit lacks known segment coverage')
        if result.get('status') == 'completed' and 'resource_gradient_collection' in job['config']:
            collection=job['config']['resource_gradient_collection']
            if collection.get('contract') in _UNIFIED_COLLECTORS and result.get('adaptive_gradient_policy',{}).get('collector_contract')!=collection['contract']:
                raise ValueError('engine did not attest the requested collector contract')
            for name, required in resource_gradient_collection_outputs(job['config']['resource_gradient_collection']).items():
                artifact = path.parent / name
                if not artifact.is_file():
                    raise ValueError('missing resource gradient collection output: ' + name)
                with artifact.open(newline='') as stream:
                    fields = csv.DictReader(stream).fieldnames or []
                if not required <= set(fields):
                    raise ValueError('resource gradient collection output schema mismatch: ' + name)
        return result


register_job_type('game', EngineJob('game'))
register_job_type('generate_map', EngineJob('generate_map'))
