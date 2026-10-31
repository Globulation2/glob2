#!/usr/bin/env python3
"""Prepare a verified downstream handoff from an existing public stable release.

Writes campaign evidence only. Never publishes, retags, assigns other people,
or marks a downstream package as tested.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tarfile
import urllib.request

REPOSITORY = 'Globulation2/glob2'
DISTROS = ('Debian', 'Ubuntu', 'Fedora', 'Arch', 'Gentoo', 'openSUSE')


def github(resource):
    request = urllib.request.Request('https://api.github.com/repos/' + REPOSITORY + '/' + resource,
                                     headers={'Accept': 'application/vnd.github+json',
                                              'User-Agent': 'glob2-downstream-release-kit'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def verify_source(tag, archive, release, commit):
    if not re.fullmatch(r'v[0-9]+(?:\.[0-9]+)+', tag):
        raise ValueError('Expected an immutable vVERSION release tag')
    if release.get('tag_name') != tag or release.get('draft') is not False or release.get('prerelease') is not False:
        raise ValueError('The target must be a published stable release')
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('Expected a resolved public commit SHA')
    version = tag[1:]
    filename = 'glob2-' + version + '.tar.gz'
    assets = [asset for asset in release['assets'] if asset['name'] == filename]
    if len(assets) != 1:
        raise ValueError('Release must contain exactly one canonical source archive')
    asset = assets[0]
    checksum = hashlib.sha256()
    with Path(archive).open('rb') as source:
        while block := source.read(1024 * 1024):
            checksum.update(block)
    digest = checksum.hexdigest()
    if asset.get('digest') != 'sha256:' + digest or asset.get('size') != Path(archive).stat().st_size:
        raise ValueError('Source archive does not match the published asset digest and size')
    with tarfile.open(archive, 'r:gz') as source:
        entry = source.extractfile('glob2-' + version + '/scons/build_layout.py')
        if entry is None or not re.search(r'^PACKAGE_VERSION = "' + re.escape(version) + r'"$',
                                         entry.read().decode(), re.MULTILINE):
            raise ValueError('Source archive version does not match the release tag')
    return dict(tag=tag, version=version, sourceCommit=commit, releaseUrl=release['html_url'],
                sourceUrl=asset['browser_download_url'], sourceFilename=filename,
                sourceSha256=digest, sourceBytes=asset['size'])


def prepare(tag, archive, coordinator, output):
    if not re.fullmatch(r'v[0-9]+(?:\.[0-9]+)+', tag):
        raise ValueError('Expected an immutable vVERSION release tag')
    if not re.fullmatch(r'[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})', coordinator):
        raise ValueError('Coordinator must be a GitHub login')
    # Resolve the tag itself, including annotated tags, rather than target_commitish
    # (which can be a moving branch name).
    release = github('releases/tags/' + tag)
    commit = github('commits/' + tag)['sha']
    identity = verify_source(tag, archive, release, commit)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    # A campaign is edited as work proceeds. Never overwrite an existing kit.
    if any(output.iterdir()):
        raise ValueError('Release kit output must be empty')
    identity['coordinator'] = coordinator
    (output / 'release.json').write_text(json.dumps(identity, indent=2) + '\n')
    (output / 'SHA256SUMS').write_text(identity['sourceSha256'] + '  ' + identity['sourceFilename'] + '\n')
    rows = '\n'.join('| ' + distro + ' | preparing | not yet recorded | not submitted | dependency/build audit pending |'
                     for distro in DISTROS)
    (output / 'campaign.md').write_text(
        '# Linux downstream campaign: ' + tag + '\n\nCoordinator: @' + coordinator + '\n\n'
        'Release: ' + identity['releaseUrl'] + '\n\nSource commit: `' + commit + '`\n\n'
        'Source SHA-256: `' + identity['sourceSha256'] + '`\n\n'
        '| Distribution | Status | Current version / maintainer | Submission | Blockers |\n'
        '| --- | --- | --- | --- | --- |\n' + rows + '\n\n'
        'Statuses: preparing → submitted → accepted → available → installed-package verified.\n\n'
        'Attach exact recipe revisions, patches, commands, build logs, native packaging checks, '
        'installation/upgrade tests and compatibility traces before claiming verification. '
        'Record feature omissions and untested architectures.\n\n'
        'Second wave: inventory NixOS, Alpine, Void and Mageia before selecting submissions.\n')
    return identity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--coordinator', required=True, help='Consenting upstream coordinator GitHub login')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        identity = prepare(args.tag, args.archive, args.coordinator, args.output)
    except (ValueError, OSError, KeyError, tarfile.TarError) as error:
        parser.exit(1, str(error) + '\n')
    print(json.dumps(identity, indent=2))


if __name__ == '__main__':
    main()
