#!/usr/bin/env python3
"""Reconcile public game releases with bounded MediaWiki sections (dry-run by default)."""
import argparse
import difflib
import http.cookiejar
import json
import os
from pathlib import Path
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

API = 'https://globulation2.org/api.php'
REPO = 'https://github.com/Globulation2/glob2'
GITHUB = 'https://api.github.com/repos/Globulation2/glob2'
PAGES = ('Download and Install', 'Changes', 'Older Releases')
BEGIN = '<!-- glob2-release-sync:begin -->'
END = '<!-- glob2-release-sync:end -->'
TAG = re.compile(r'v?[0-9]+(?:\.[0-9]+){2,3}\Z')


class SyncError(RuntimeError):
    pass


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise SyncError('Unexpected HTTP redirect; refusing to forward credentials')


class Transport:
    def __init__(self):
        self.opener = urllib.request.build_opener(
            NoRedirect(), urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))

    def __call__(self, url, data=None):
        request = urllib.request.Request(url, data=data, headers={
            'User-Agent': 'Globulation2-WikiReleaseSync/1.0',
            'Accept': 'application/json'})
        try:
            with self.opener.open(request, timeout=30) as response:
                return json.load(response)
        except (urllib.error.URLError, ValueError) as error:
            # Never include request URLs, payloads or response bodies in errors.
            raise SyncError('HTTP or JSON failure; no wiki credentials or response body logged') from None


def releases(fetch):
    result = []
    page = 1
    while True:
        batch = fetch(f'{GITHUB}/releases?per_page=100&page={page}')
        if not isinstance(batch, list):
            raise SyncError('Invalid GitHub releases response')
        if not batch:
            break
        for release in batch:
            if not release.get('draft') and TAG.fullmatch(release.get('tag_name', '')):
                if not re.fullmatch(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z', release.get('published_at') or ''):
                    raise SyncError('Missing or invalid public release date')
                result.append(release)
        page += 1
    latest = fetch(f'{GITHUB}/releases/latest')
    if latest.get('draft') or latest.get('prerelease') or not TAG.fullmatch(latest.get('tag_name', '')):
        raise SyncError('GitHub latest is not a stable game release')
    if latest['tag_name'] not in {row['tag_name'] for row in result}:
        raise SyncError('Latest release missing from inventory')
    return sorted(result, key=lambda r: (r['published_at'], r['tag_name']), reverse=True), latest['tag_name']


def entry(release):
    # Only numeric tags and validated ISO dates enter wikitext; never render GitHub bodies/names.
    tag = release['tag_name']
    status = 'preview' if release.get('prerelease') else 'stable'
    return f"* [{REPO}/releases/tag/{tag} {tag}] — {release['published_at'][:10]} ({status}; release notes and available downloads)"


def render(title, inventory, latest):
    stable = [r for r in inventory if not r.get('prerelease')]
    previews = [r for r in inventory if r.get('prerelease')]
    if title == 'Download and Install':
        lines = ['== Current downloads ==',
                 f"Latest stable release: '''[{REPO}/releases/tag/{latest} {latest}]'''.",
                 '[https://glob2.online/ Current website and downloads] · '
                 '[https://app.glob2.online/ Play in your browser]',
                 'See the linked release for its available packages and notes. Platform availability varies by release.',
                 'Source code: [[Git|current repository and build instructions]].',
                 '=== Public previews ===']
        lines += [entry(r) for r in previews] or ['No public versioned previews are currently listed.']
        lines += ['See [[Changes]] and [[Older Releases]] for release history.']
    else:
        lines = ['== Public game releases ==' if title == 'Changes' else '== Modern release archive ==',
                 f'Latest stable: [{REPO}/releases/tag/{latest} {latest}]. See [[Download and Install]] for current downloads.',
                 'The links below provide the authoritative release notes and available downloads.',
                 '=== Stable releases ===']
        lines += [entry(r) for r in stable if title != 'Older Releases' or r['tag_name'] != latest] or ['No earlier stable releases are listed.']
        lines += ['=== Public previews ===']
        lines += [entry(r) for r in previews] or ['No public versioned previews are currently listed.']
    return BEGIN + '\n' + '\n\n'.join(lines) + '\n' + END


def replace_section(text, generated, initialize=False):
    if BEGIN not in text and END not in text and initialize:
        # Keep an existing translations template above the new content.
        match = re.match(r'(\{\{Translations[^\n]*\}\}\s*\n)', text)
        prefix = match.group(0) if match else ''
        legacy = text[len(prefix):]
        return prefix + generated + '\n\n== Historical information ==\nThese archived instructions and notes describe older releases. Use the current links above for modern versions.\n\n' + legacy
    if text.count(BEGIN) != 1 or text.count(END) != 1 or text.index(BEGIN) > text.index(END):
        raise SyncError('Missing, duplicate or reversed managed section markers')
    start, end = text.index(BEGIN), text.index(END) + len(END)
    return text[:start] + generated + text[end:]


class Wiki:
    def __init__(self, fetch):
        self.fetch = fetch

    def call(self, **params):
        params.update(format='json')
        if 'assert_' in params:
            params['assert'] = params.pop('assert_')
        result = self.fetch(API, urllib.parse.urlencode(params).encode())
        if 'error' in result:
            code = result['error'].get('code', '')
            raise SyncError('MediaWiki API rejected request (' + re.sub(r'[^a-zA-Z0-9_-]', '', code)[:60] + ')')
        return result

    def login(self, username, password):
        if not username or not password:
            raise SyncError('Wiki username and password are required')
        token = self.call(action='query', meta='tokens', type='login')['query']['tokens']['logintoken']
        if '@' in username:
            result = self.call(action='login', lgname=username, lgpassword=password, lgtoken=token)
            success = result.get('login', {}).get('result') == 'Success'
        else:
            result = self.call(action='clientlogin', username=username, password=password,
                               logintoken=token, loginreturnurl='https://globulation2.org/wiki/Main_Page',
                               loginmessageformat='none')
            success = result.get('clientlogin', {}).get('status') == 'PASS'
        if not success:
            raise SyncError('Wiki login failed or requires interaction; no automatic retry')

    def read(self, title):
        data = self.call(action='query', prop='revisions|info', titles=title,
                         rvprop='content|ids|timestamp', intestactions='edit', curtimestamp=1)
        pages = list(data['query']['pages'].values())
        if len(pages) != 1 or 'missing' in pages[0] or pages[0].get('title') != title:
            raise SyncError('Expected existing wiki page unavailable')
        page = pages[0]
        revision = page['revisions'][0]
        return {'title': title, 'text': revision['*'], 'revid': revision['revid'],
                'timestamp': revision['timestamp'], 'start': data['curtimestamp'],
                'editable': isinstance(page.get('actions'), dict) and
                page['actions'].get('edit', False) in ('', True)}

    def save(self, page, content, token):
        result = self.call(action='edit', title=page['title'], text=content, token=token,
                           baserevid=page['revid'], basetimestamp=page['timestamp'],
                           starttimestamp=page['start'], assert_='user', nocreate=1,
                           summary='Synchronize public stable and preview releases from GitHub')
        edit = result.get('edit', {})
        if edit.get('result') != 'Success' or 'captcha' in edit:
            raise SyncError('Wiki edit failed or requires CAPTCHA; no automatic retry')
        return edit.get('newrevid', page['revid'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--publish', action='store_true')
    parser.add_argument('--initialize', action='store_true', help='Explicit one-time insertion before historical content')
    parser.add_argument('--output-dir', type=Path, help='Write public proposed page text for review')
    args = parser.parse_args()
    fetch = Transport()
    inventory, latest = releases(fetch)
    wiki = Wiki(fetch)
    if args.publish:
        wiki.login(os.environ.get('WIKI_USERNAME', os.environ.get('WIKI_BOT_USERNAME', '')),
                   os.environ.get('WIKI_PASSWORD', os.environ.get('WIKI_BOT_PASSWORD', '')))
    proposals = []
    for title in PAGES:
        page = wiki.read(title)
        if args.publish and not page['editable']:
            raise SyncError('Account lacks edit permission on ' + title)
        content = replace_section(page['text'], render(title, inventory, latest), args.initialize)
        proposals.append((page, content))
    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
    token = None
    for page, content in proposals:
        title = page['title']
        if args.output_dir:
            (args.output_dir / (title.replace(' ', '_') + '.wiki')).write_text(content)
        if content == page['text']:
            print(f'Unchanged: {title}')
            continue
        if not args.publish:
            print(''.join(difflib.unified_diff(page['text'].splitlines(True), content.splitlines(True),
                                             fromfile=title, tofile=title + ' (proposed)')))
        else:
            if token is None:
                token = wiki.call(action='query', meta='tokens')['query']['tokens']['csrftoken']
            revision = wiki.save(page, content, token)
            print(f'Updated: {title}: https://globulation2.org/index.php?oldid={revision}')


if __name__ == '__main__':
    try:
        main()
    except (SyncError, KeyError, TypeError) as error:
        print('Wiki synchronization failed: ' + (str(error) if isinstance(error, SyncError) else 'unexpected API response'), file=sys.stderr)
        sys.exit(1)
