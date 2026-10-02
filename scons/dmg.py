"""Create the release disk image without expanding bundled dylib aliases."""
from pathlib import Path
import shutil
import subprocess
import tempfile


def create_dmg(target, source, env):
    image = Path(str(target[0])).resolve()
    image.parent.mkdir(parents=True, exist_ok=True)
    image.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix='glob2-dmg-', dir=image.parent) as temporary:
        stage = Path(temporary)
        for item in source:
            path = Path(str(item))
            shutil.copytree(path, stage/path.name, symlinks=True)
        subprocess.run(['hdiutil', 'create', '-srcfolder', str(stage),
                        '-volname', image.stem, '-format', 'UDZO', str(image)], check=True)


def create_dmg_message(target, source, env):
    return 'Creating DMG package'


def generate(env):
    print('Loading dmg tool...')
    env.Append(BUILDERS={'Dmg': env.Builder(action=env.Action(create_dmg, create_dmg_message))})


def exists(env):
    return True
