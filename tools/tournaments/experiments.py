"""Four experiment planners. All execution goes through Coordinator."""
import argparse
import itertools
import json
import random
from pathlib import Path

from .analysis import POLICIES, reanalyze
from .bundles import inspect_bundle
from .common import atomic_json, digest, read_json
from .coordinator import Coordinator
from .model import grid, job, rotations, seeded_samples, validate_experiment


class Planner:
    def __init__(self, kind, config, bundles):
        self.kind, self.config = kind, config
        self.bundles = {bundle['id']: bundle for bundle in bundles}
        self.builds = config.get('builds', list(self.bundles))
        if not self.builds or any(build not in self.bundles for build in self.builds):
            raise ValueError('each build cohort needs a supplied bundle')
        self.map_build = config.get('map_build', self.builds[0])
        if self.map_build not in self.bundles:
            raise ValueError('map_build needs a supplied bundle')
        self.jobs, self.maps = [], {}

    def generated(self, method, seed, n, variant=None):
        config = self.config
        default_params = {'width': 7, 'height': 7} if self.kind == 'ai_comparison' else {}
        generator = {'generator': method, 'params': dict(config.get('generator_params', default_params), teams=n),
                     'candidates': config.get('candidates', 5), 'rotations': n}
        if variant:
            generator['params'].update(variant)
        key = digest([generator, seed, self.map_build])
        if key not in self.maps:
            value = job('generate_map', self.map_build, seeds={'map': seed}, config=generator,
                        outputs={'map': True}, limits={'timeout_seconds': config.get('generation_timeout_seconds', 1800)},
                        labels={'map_seed': seed, 'generator': method, 'map': f'{method}:{seed}', 'variant': key[:8]})
            self.jobs.append(value); self.maps[key] = value
        return self.maps[key]

    def sampled_game(self, rng, build, ais, methods, sizes, formats):
        """One independently-drawn game: format, AI matchup, generator and map
        size are each sampled fresh, using the engine's inline map-generation
        (a single job, no separate generate_map dependency) -- for a broad but
        bounded random sample instead of the exhaustive cross product plan()
        otherwise builds. Reuses the same job/label shape as game() so
        analysis.py's observations()/rate() need no changes to read either."""
        config = self.config
        fmt = rng.choice(formats)
        n = 2 if fmt == '1v1' else 4
        if fmt == '2v2':
            a, b = rng.sample(ais, 2) if len(ais) >= 2 else (ais[0], ais[0])
            players, alliances = [a, a, b, b], [1, 1, 2, 2]
        else:
            players = rng.sample(ais, n) if len(ais) >= n else [rng.choice(ais) for _ in range(n)]
            alliances = None
        method = rng.choice(methods)
        params = dict(rng.choice(sizes), teams=n)
        map_seed, game_seed = rng.getrandbits(32), rng.getrandbits(32)
        labels = {'format': fmt, 'generator': method, 'map_seed': map_seed, 'map': f'{method}:{map_seed}',
                  'rotation': 0, 'variant': 'baseline', 'subject_player': config.get('player', 0),
                  'symmetric_control': method == 15, 'block': f'{method}:{map_seed}:{game_seed}'}
        value = job('game', build, seeds={'map': map_seed, 'game': game_seed},
                    config={'generator': method, 'params': params, 'candidates': config.get('candidates', 5),
                            'players': players, 'ticks': config.get('ticks', 90000), 'ai_params': {},
                            **({'alliances': alliances} if alliances else {}),
                            **({'win_probability_permille': config['win_probability_permille']}
                               if config.get('win_probability_permille') else {})},
                    outputs=config.get('outputs', {}), limits={'timeout_seconds': config.get('timeout_seconds', 3600)},
                    labels=labels)
        self.jobs.append(value)

    def game(self, generated, build, seed, players, rotation, fmt, variant='baseline', overrides=None, pair=None, held_out=False, alliances=None):
        config = self.config
        map_seed = generated['seeds']['map']
        method = generated['config']['generator']
        labels = {'format': fmt, 'map': f'{method}:{map_seed}', 'map_seed': map_seed, 'generator': method,
                  'subject_player': config.get('player', 0), 'rotation': rotation, 'variant': variant, 'pair': pair, 'held_out': held_out,
                  'symmetric_control': method == 15, 'block': f'{method}:{map_seed}:{seed}'}
        value = job('game', build,
                    inputs={'map': {'job': generated['id'], 'artifact': f'map-r{rotation}.map'}},
                    depends_on=[generated['id']], seeds={'game': seed},
                    config={'players': players, 'ticks': config.get('ticks', 90000),
                            'ai_params': overrides or {}, **({'alliances': alliances} if alliances else {}),
                            **({'win_probability_permille': config['win_probability_permille']}
                               if config.get('win_probability_permille') else {})},
                    outputs=config.get('outputs', {}), limits={'timeout_seconds': config.get('timeout_seconds', 3600)},
                    labels=labels)
        self.jobs.append(value)

    def plan(self):
        config = self.config
        methods = config.get('generators', [15])
        seeds = config.get('map_seeds', [1001])
        game_seeds = config.get('game_seeds', [1])
        if self.kind == 'generator_stress':
            variants = [('baseline', config.get('generator_params', {}))]
            variants += [(f'grid-{i}', params) for i,params in enumerate(grid(config['grid']))] if config.get('grid') else []
            variants += [(f'sample-{i}', params) for i,params in enumerate(seeded_samples(config['sample'], config.get('samples',32), config.get('sample_seed',1)))] if config.get('sample') else []
            for build, method, seed, (name, params) in itertools.product(self.builds, methods, seeds, variants):
                self.jobs.append(job('generate_map',build,seeds={'map':seed},
                                     config={'generator':method,'params':params,'candidates':config.get('candidates',0)},
                                     outputs=config.get('outputs',{}), limits={'timeout_seconds':config.get('timeout_seconds',60)},
                                     labels={'variant':name,'map_seed':seed,'generator':method}))
        elif self.kind == 'fairness':
            n = config.get('colonies',4)
            for method, map_seed in itertools.product(methods,seeds):
                generated = self.generated(method,map_seed,n)
                for build, game_seed, rotation in itertools.product(self.builds,game_seeds,range(n)):
                    self.game(generated,build,game_seed,[config.get('ai','nicowar')]*n,rotation,'ffa')
        elif self.kind == 'ablations':
            variants = [('baseline',{})]
            variants += [(f'{key}={value}',{key:value}) for key,values in sorted(config.get('one_parameter',{}).items()) for value in values]
            variants += [(f'factorial-{i}',v) for i,v in enumerate(grid(config['grid']))] if config.get('grid') else []
            variants += [(f'random-{i}',v) for i,v in enumerate(seeded_samples(config['sample'],config.get('samples',32),config.get('sample_seed',1)))] if config.get('sample') else []
            players = config.get('players',['cortex','nicowar'])
            n = len(players)
            held_out = config.get('held_out_map_seeds',[])
            if set(held_out) & set(seeds): raise ValueError('held-out seeds must be disjoint')
            for method, map_seed, (name, params) in itertools.product(methods,seeds+held_out,variants):
                generator_variant = params if config.get('target','ai')=='generator' else None
                generated = self.generated(method,map_seed,n,generator_variant)
                for build, game_seed, rotation in itertools.product(self.builds,game_seeds,range(n)):
                    overrides = config.get('baseline_ai_params',{})
                    overrides = json.loads(json.dumps(overrides))
                    if config.get('target','ai')=='ai':
                        target = str(config.get('player',0))
                        overrides[target] = overrides.get(target,{}) | params
                    pair = f'{method}:{map_seed}:{game_seed}:{rotation}'
                    self.game(generated,build,game_seed,players,rotation,config.get('format','1v1' if n==2 else 'ffa'),
                              name,overrides,pair,map_seed in held_out,config.get('alliances'))
        else:
            ais = config.get('ais') or [a['name'] for a in self.bundles[self.builds[0]]['capabilities']['ais'] if a['id'] != 0]
            if not ais: raise ValueError('AI comparison needs selectable active AIs')
            # AI Elo scoring wants each game's internal AI decisions available for
            # later analysis (e.g. offense-target-team fields), not just engine
            # outcomes -- default this on for ai_comparison specifically rather
            # than for every Planner kind (fairness/ablations/generator_stress
            # keep their own outputs default of none, set below via self.game()).
            config.setdefault('outputs', {'telemetry': ['team-timeline']})
            # The standard Elo cohort is a cheap, directly comparable duel at one
            # map size. Callers can still request teams or FFA explicitly.
            formats = config.get('formats',['1v1'])
            for fmt in formats:
                if fmt not in ('1v1','2v2','ffa'): raise ValueError('unknown format')
            if config.get('sample_games'):
                # A bounded random sample instead of the exhaustive cross product
                # below: each of sample_games draws its own format/matchup/
                # generator/size independently, rather than every combination of
                # every AI x format x generator x seed x size (which can reach
                # hundreds of thousands of games -- see docs/tournaments.md).
                rng = random.Random(config.get('sample_seed', 1))
                sample_methods = config.get('generators') or [
                    g['method'] for g in self.bundles[self.builds[0]]['capabilities']['generators'] if not g.get('editorOnly')]
                default_params = {'width': 7, 'height': 7}
                sizes = config.get('sizes') or [config.get('generator_params', default_params)]
                for _ in range(config['sample_games']):
                    self.sampled_game(rng, rng.choice(self.builds), ais, sample_methods, sizes, formats)
            else:
                for fmt in formats:
                    n = 2 if fmt=='1v1' else 4
                    if fmt == '2v2':
                        rosters = config.get('rosters') or [[ai,ai] for ai in ais]
                        if any(len(roster)!=2 for roster in rosters): raise ValueError('2v2 rosters must contain two AIs')
                        schedules = [(a+b,[1,1,2,2]) for a,b in itertools.combinations(rosters,2)]
                    elif fmt == '1v1':
                        schedules = [(list(pair),None) for pair in itertools.combinations(ais,2)]
                    else:
                        selected = list(itertools.combinations(ais,n)) if len(ais)>=n else [tuple((ais*n)[:n])]
                        schedules = [(list(group),None) for group in selected]
                    if not schedules: schedules=[(([ais[0]]*n),[1,1,2,2] if fmt=='2v2' else None)]
                    for method, map_seed in itertools.product(methods,seeds):
                        generated = self.generated(method,map_seed,n)
                        for build, game_seed, rotation, (players,allies), order in itertools.product(self.builds,game_seeds,range(n),schedules,range(n)):
                            ordered = rotations(players)[order]
                            groups = rotations(allies)[order] if allies else None
                            self.game(generated,build,game_seed,ordered,rotation,fmt,alliances=groups)
        # Cyclic rotations of homogeneous rosters can produce identical logical
        # jobs. Keep one occurrence: repeated identical attempts add no information.
        self.jobs = list({value['id']:value for value in self.jobs}.values())
        manifest = {'schema_version':1,'id':config['id'],'kind':self.kind,'jobs':self.jobs,
                    'settings':config.get('settings',{}),'labels':config.get('labels',{}),
                    'design':config}
        return validate_experiment(manifest)


def experiment_main(kind):
    parser = argparse.ArgumentParser(description=f'{kind}: plan, submit, and offline reanalysis')
    sub = parser.add_subparsers(dest='command',required=True)
    for name in ('plan','submit'):
        p=sub.add_parser(name);p.add_argument('config');p.add_argument('--bundle',action='append',required=True)
        p.add_argument('--output',required=True)
    p=sub.add_parser('reanalyze');p.add_argument('directory');p.add_argument('--policy',choices=POLICIES,default='prestige')
    p.add_argument('--seed',type=int,default=1);p.add_argument('--draws',type=int,default=1000);p.add_argument('--k',type=float,default=32)
    p.add_argument('--output')
    args=parser.parse_args()
    try:
        if args.command=='reanalyze':
            report=reanalyze(args.directory,args.policy,args.draws,args.seed,args.k,args.output)
            print(json.dumps({'results':report['committed_results'],'ratings':report['ratings']},indent=2))
        else:
            manifest=Planner(kind,read_json(args.config),[inspect_bundle(p) for p in args.bundle]).plan()
            if args.command=='plan': atomic_json(args.output,manifest)
            else:
                coordinator=Coordinator.submit(args.output,manifest,args.bundle)
                coordinator.close()
            print(json.dumps({'jobs':len(manifest['jobs']),'output':args.output}))
    except (OSError,ValueError) as error:
        parser.exit(2,f'error: {error}\n')
