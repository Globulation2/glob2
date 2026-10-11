#!/usr/bin/env python3
"""Verify an owner-dispatched source run before retrying Mac artifact signing."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile

REPOSITORY = 'genixpro/glob2-release'
REPOSITORY_ID = 1397722696
OWNER_ID = 6193625


def gh(path, binary=False):
    command = ['gh', 'api']
    if binary:
        command.append('--allow-escape-sequences')
    result = subprocess.run(command + [path], capture_output=True)
    if result.returncode:
        raise ValueError('GitHub release evidence could not be retrieved')
    return result.stdout if binary else json.loads(result.stdout)


def paginated(path, field):
    items = []
    for page in range(1, 101):
        data = gh(path + ('&' if '?' in path else '?') + f'per_page=100&page={page}')[field]
        items.extend(data)
        if len(data) < 100:
            return items
    raise ValueError('Release evidence pagination limit exceeded')


def validate(run, jobs, artifacts, context_log, tag, commit):
    if not re.fullmatch(r'v[0-9]+(?:\.[0-9]+)+', tag) or not re.fullmatch('[0-9a-f]{40}', commit):
        raise ValueError('Invalid immutable source identity')
    if (run.get('repository', {}).get('id') != REPOSITORY_ID or
            run.get('repository', {}).get('full_name') != REPOSITORY or
            run.get('actor', {}).get('id') != OWNER_ID or
            run.get('triggering_actor', {}).get('id') != OWNER_ID or
            run.get('event') != 'workflow_dispatch' or run.get('head_branch') != 'master' or
            run.get('path') != '.github/workflows/github-release.yml'):
        raise ValueError('Source run is outside the protected mirror owner-dispatch boundary')
    # A failed signing job is allowed; its source build and context must pass.
    required = {'context', 'packages / macos-package (macos-15, arm64)',
                'packages / macos-package (macos-15-intel, x86_64)'}
    for name in required:
        matching = [job for job in jobs if job.get('name') == name]
        if not matching or any(job.get('status') != 'completed' or job.get('conclusion') != 'success'
                               for job in matching):
            raise ValueError('Source context or Mac package job is not successfully completed')
    log = re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', context_log)
    revisions = re.findall(r'(?m)^\S+\s+([0-9a-f]{40})\s*$', log)
    tags = re.findall(r'(?m)^\S+\s+TAG: (v[0-9]+(?:\.[0-9]+)+)\s*$', log)
    if revisions != [commit] or tags != [tag]:
        raise ValueError('Source run checkout or immutable tag differs from the requested release')
    selected = {}
    for architecture in ('arm64', 'x86_64'):
        matching = [artifact for artifact in artifacts
                    if artifact.get('name') == 'unsigned-macos-' + architecture]
        if len(matching) != 1:
            raise ValueError('Expected exactly one immutable unsigned Mac artifact')
        artifact = matching[0]
        if (artifact.get('expired') is not False or not isinstance(artifact.get('id'), int) or
                not re.fullmatch('sha256:[0-9a-f]{64}', artifact.get('digest') or '')):
            raise ValueError('Unsigned Mac artifact is expired or lacks an immutable digest')
        selected[architecture] = {'id': artifact['id'], 'digest': artifact['digest']}
    return selected


def extract(archive, expected_digest, output, version):
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected_digest.removeprefix('sha256:'):
        raise ValueError('Unsigned artifact checksum differs from verified source-run evidence')
    expected = f'Glob2-{version}.dmg'
    with zipfile.ZipFile(archive) as package:
        files = [entry for entry in package.infolist() if not entry.is_dir()]
        if len(files) != 1 or files[0].filename != expected or files[0].file_size > 2 * 1024**3:
            raise ValueError('Unsigned artifact has unexpected or unsafe package contents')
        if (files[0].external_attr >> 16) & 0o170000 == 0o120000:
            raise ValueError('Unsigned artifact cannot contain a symlink')
        output.mkdir(parents=True, exist_ok=False)
        with (output / expected).open('xb') as target, package.open(files[0]) as source:
            import shutil
            shutil.copyfileobj(source, target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest='mode', required=True)
    preflight = modes.add_parser('preflight')
    preflight.add_argument('--run-id', type=int, required=True)
    preflight.add_argument('--tag', required=True)
    preflight.add_argument('--source-commit', required=True)
    preflight.add_argument('--github-output', type=Path, required=True)
    download = modes.add_parser('download')
    download.add_argument('--artifact-id', type=int, required=True)
    download.add_argument('--digest', required=True)
    download.add_argument('--version', required=True)
    download.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.mode == 'preflight':
        if args.run_id <= 0:
            raise ValueError('Source run ID must be positive')
        base = f'repos/{REPOSITORY}/actions/runs/{args.run_id}'
        run = gh(base)
        jobs = paginated(base + '/jobs?filter=all', 'jobs')
        contexts = [job for job in jobs if job.get('name') == 'context' and job.get('conclusion') == 'success']
        if not contexts:
            raise ValueError('Source run requires a successful context job')
        artifacts = paginated(base + '/artifacts', 'artifacts')
        for context in contexts:
            context_log = gh(f'repos/{REPOSITORY}/actions/jobs/{context["id"]}/logs', binary=True).decode()
            selected = validate(run, jobs, artifacts, context_log, args.tag, args.source_commit)
        with args.github_output.open('a') as stream:
            stream.write('artifacts=' + json.dumps(selected, separators=(',', ':')) + '\n')
        print('Owner-dispatched source revision and both immutable Mac artifact digests verified')
    else:
        if args.artifact_id <= 0 or not re.fullmatch('sha256:[0-9a-f]{64}', args.digest):
            raise ValueError('Invalid verified artifact identity')
        if not re.fullmatch(r'[0-9]+(?:\.[0-9]+)+', args.version):
            raise ValueError('Invalid package version')
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / 'artifact.zip'
            with archive.open('wb') as stream:
                result = subprocess.run(['gh', 'api', f'repos/{REPOSITORY}/actions/artifacts/{args.artifact_id}/zip'],
                                        stdout=stream, stderr=subprocess.PIPE)
            if result.returncode:
                raise ValueError('Verified unsigned artifact download failed')
            extract(archive, args.digest, args.output, args.version)
        print('Exact unsigned Mac artifact digest and package contents verified')


if __name__ == '__main__':
    main()
