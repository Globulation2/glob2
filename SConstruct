import sys
from pathlib import Path
from SCons.Script import Environment, Action, GetOption, Default
sys.path.insert(0, str(Path.cwd()))
from tools.package_assets import export_assets
root = Path('artifacts/parallel-assets/scons-fixture').resolve()
(root / 'data/gfx').mkdir(parents=True, exist_ok=True)
import shutil
for index, source in enumerate(sorted(Path('data/gfx').glob('gamegui*.png'))[:4]):
    shutil.copyfile(source, root / 'data/gfx' / f'image{index}.png')
output = Path('artifacts/parallel-assets/scons-output').resolve()
def export(target, source, env):
    audit = export_assets(root, output, jobs=GetOption('num_jobs'))
    assert len(audit['files']) == 4
    return 0
env = Environment()
Default(env.Command(str(output.with_suffix('.json')), [str(p) for p in (root/'data/gfx').glob('*.png')], Action(export, 'Parallel export integration')))
