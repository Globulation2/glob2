"""Source identity shared by build-time producers and evidence runners."""
import hashlib
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def source_identity(root=ROOT):
    def git(*args):
        return subprocess.check_output(['git', *args], cwd=root)
    # Include untracked, nonignored inputs in development builds. Evidence cannot
    # claim a clean revision if any of them, or any tracked inputs, are modified.
    paths = set(git('ls-files', '-z', '--cached', '--others', '--exclude-standard').split(b'\0'))
    digest = hashlib.sha256()
    for relative in sorted(paths - {b''}):
        path = root / relative.decode('utf-8')
        data = path.read_bytes() if path.is_file() else b'<missing>'
        digest.update(relative + b'\0' + hashlib.sha256(data).digest())
    return {'revision': git('rev-parse', 'HEAD').decode().strip(),
            'dirty': bool(git('status', '--porcelain').strip()),
            'sourceTreeSha256': digest.hexdigest()}


def build_issues(build, source):
    if not isinstance(build, dict) or not build:
        return ['Missing executed-binary build provenance']
    return ['Executed binary differs from runner source: ' + key
            for key in ('revision', 'dirty', 'sourceTreeSha256')
            if key not in source or build.get(key) != source[key]]


if __name__ == '__main__':
    import json
    print(json.dumps(source_identity(), sort_keys=True))
