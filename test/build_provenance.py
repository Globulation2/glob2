"""Source identity shared by build-time producers and evidence runners."""
import hashlib
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def source_identity(root=ROOT):
    def git(*args):
        return subprocess.check_output(['git', *args], cwd=root)
    # Refresh cached index stats first, so touching identical content does not
    # masquerade as a changed worktree input in diff-files.
    status = git('status', '--porcelain')
    dirty = bool(status.strip())
    # Git's blob representation normalizes checkout-only CRLF and symlink
    # differences. Hash dirty worktree inputs too; an edited binary cannot be
    # relabeled as its unchanged HEAD, even during development runs.
    entries = {}
    for record in git('ls-files', '--stage', '-z').split(b'\0'):
        if not record:
            continue
        metadata, relative = record.split(b'\t', 1)
        mode, blob, stage = metadata.split()
        if stage != b'0':
            raise ValueError('Cannot identify a source tree with unresolved Git conflicts')
        entries[relative] = mode + b' ' + blob
    for relative in git('diff-files', '--name-only', '-z').split(b'\0'):
        if not relative:
            continue
        path = root / relative.decode('utf-8')
        if path.is_symlink():
            data = str(path.readlink()).encode('utf-8')
            identity = hashlib.sha256(data).hexdigest().encode()
        elif path.is_file():
            name = relative.decode('utf-8')
            identity = git('hash-object', '--path=' + name, name).strip()
        else:
            identity = b'<missing>'
        entries[relative] = b'worktree ' + identity
    for relative in git('ls-files', '--others', '--exclude-standard', '-z').split(b'\0'):
        if not relative:
            continue
        path = root / relative.decode('utf-8')
        data = str(path.readlink()).encode('utf-8') if path.is_symlink() else path.read_bytes()
        entries[relative] = b'untracked ' + hashlib.sha256(data).hexdigest().encode()
    digest = hashlib.sha256()
    for relative, identity in sorted(entries.items()):
        digest.update(relative + b'\0' + identity + b'\0')
    return {'revision': git('rev-parse', 'HEAD').decode().strip(),
            'dirty': dirty,
            'sourceStatus': status.decode('utf-8', errors='replace').splitlines(),
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
