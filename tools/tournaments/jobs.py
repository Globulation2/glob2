"""Job-type adapters; scheduling and transport have no experiment-specific logic."""
from pathlib import Path
from .common import read_json

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
        config, seeds = job['config'], job['seeds']
        generation = {'generator', 'params', 'candidates'}
        allowed = ({'players', 'ticks', 'ai_params', 'alliances', 'winning_conditions',
                    'win_probability_permille'} | generation
                   if self.kind == 'game' else generation | {'rotations'})
        if set(config) - allowed:
            raise ValueError('unknown job configuration fields: ' + ', '.join(sorted(set(config) - allowed)))
        if self.kind == 'game' and 'save' in job['inputs'] and seeds:
            raise ValueError('saved games retain their seeds')
        if self.kind == 'game':
            # A game either loads a map or a save, or generates its own map inline;
            # exactly one of the three.
            inline = 'generator' in config
            sources = int('map' in job['inputs']) + int('save' in job['inputs']) + int(inline)
            if sources != 1:
                raise ValueError('game requires exactly one map, saved state or inline generator')
            if inline:
                if type(config['generator']) is not int or 'map' not in seeds:
                    raise ValueError('inline generation requires a map seed and integer generator')
                if not config.get('players') or 'game' not in seeds:
                    raise ValueError('new game requires players and game seed')
            if 'map' in job['inputs'] and (not config.get('players') or 'game' not in seeds):
                raise ValueError('new game requires players and game seed')
            if 'save' in job['inputs'] and any(k in config for k in ('players', 'alliances', 'ai_params', 'winning_conditions', 'win_probability_permille')):
                raise ValueError('cannot override a saved game configuration')
            if type(config.get('ticks', 90000)) is not int or config.get('ticks', 90000) < 1:
                raise ValueError('ticks must be a positive integer')
            if config.get('alliances') and len(config['alliances']) != len(config.get('players', [])):
                raise ValueError('one alliance required per player')
            permille = config.get('win_probability_permille')
            if permille is not None and (type(permille) is not int or not 0 <= permille <= 1000):
                raise ValueError('win_probability_permille must be a permille between 0 and 1000')
        elif 'map' not in seeds or type(config.get('generator')) is not int:
            raise ValueError('generation requires map seed and integer generator')
        if set(job['outputs']) - {'replay', 'saves', 'telemetry', 'map', 'reports', 'required', 'core', 'stack', 'result'}:
            raise ValueError('unknown requested output')
        if job['outputs'].get('result', 'full') not in ('full', 'outcome'):
            raise ValueError('result output must be full or outcome')

    def command(self, job, bundle, attempt_dir, inputs):
        root = Path(bundle['directory'])
        out = Path(attempt_dir) / 'output'
        args = [str(root / bundle['executable']),
                '--run-game' if self.kind == 'game' else '--generate-map',
                '--output-dir', str(out), '--profile', 'glob2-tournament-' + Path(attempt_dir).name]
        config, outputs = job['config'], job['outputs']
        def add(key, value):
            args.extend([key, str(value)])
        if self.kind == 'game':
            if 'generator' in config:
                add('--generator', config['generator'])
                add('--map-seed', job['seeds']['map'])
                for key, value in sorted(config.get('params', {}).items()):
                    add('--param', f'{key}={value}')
                add('--candidates', config.get('candidates', 0))
            else:
                add('--map-file' if 'map' in inputs else '--load-game',
                    inputs.get('map', inputs.get('save')))
            if 'game' in job['seeds']:
                add('--game-seed', job['seeds']['game'])
            add('--ticks', config.get('ticks', 90000))
            for player in config.get('players', []):
                add('--player', player)
            for player, params in sorted(config.get('ai_params', {}).items()):
                for key, value in sorted(params.items()):
                    add('--ai-param', f'{player}:{key}={str(value).lower() if isinstance(value, bool) else value}')
            for group in config.get('alliances', []):
                add('--alliance', group)
            for condition in config.get('winning_conditions', []):
                add('--win-condition', condition)
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
            add('--replay', str(outputs.get('replay', False)).lower())
        else:
            add('--generator', config['generator'])
            add('--map-seed', job['seeds']['map'])
            for key, value in sorted(config.get('params', {}).items()):
                add('--param', f'{key}={value}')
            add('--candidates', config.get('candidates', 0))
            add('--rotations', config.get('rotations', 1))
            add('--write-map', str(outputs.get('map', False)).lower())
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
        return result


register_job_type('game', EngineJob('game'))
register_job_type('generate_map', EngineJob('generate_map'))
